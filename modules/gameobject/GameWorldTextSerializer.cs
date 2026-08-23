// SPDX-License-Identifier: MIT
// GameWorldTextSerializer.cs —— 可读文本格式序列化器（O3 工件 2，契约 R26 / O3-可读格式契约草案.md）
//
// GameWorldSnapshot ↔ 文本 双向编码：确定性、可读、可 diff、Git 友好。
// - 格式权威：doc/plans/object-components/O3-可读格式契约草案.md（改格式先改文档）。
// - 形态 = 平铺 + DFS 序号引用（定稿草案 §2.1）：对象平铺 + parent = #序号 + [component] 块头切块；对象名仅展示。
// - Capture→Serialize→Deserialize→Restore→Capture hash 相等；同快照 Serialize 两次字节相等；幂等。
// - Deserialize 只做语法→快照；未知组件/属性的语义容错在 Restore 层（RestoreOptions，R24）。

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Baize.GameObject;

/// <summary>
/// 可读文本格式序列化器（契约 R26）：GameWorldSnapshot ↔ 文本。
/// 格式见 O3-可读格式契约草案.md：头部 + 平铺对象行（parent = #序号）+ [component] 块头 + 关系行。
/// </summary>
public static class GameWorldTextSerializer
{
	/// <summary>格式名（第一行，固定值）。</summary>
	public const string FormatName = "baize.object-components.v1";

	/// <summary>kind = scene（本文档范围；prefab 由 O4 定义）。</summary>
	public const string KindScene = "scene";

	/// <summary>导出快照为可读文本（不修改快照；对象名仅展示可重复，序号即身份，草案 §3.3）。</summary>
	public static string Serialize(GameWorldSnapshot snapshot)
	{
		ArgumentNullException.ThrowIfNull(snapshot);

		// 评审修订：Serialize 前置快照校验（DFS 合法性 + 非法 ParentIndex 抛错，禁止静默降级）。
		ValidateSnapshot(snapshot);

		var sb = new StringBuilder();
		sb.Append("format = \"").Append(FormatName).Append('"').Append('\n');
		sb.Append("kind = \"").Append(KindScene).Append('"').Append('\n');
		sb.Append('\n');

		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			var record = snapshot.Objects[i];
			sb.Append("object #").Append(i).Append(" \"").Append(Escape(record.Name)).Append('"');
			if (record.ParentIndex >= 0 && record.ParentIndex < i) // 父索引 < 子索引（DFS 序契约 + 引用校验）
			{
				sb.Append(" parent = #").Append(record.ParentIndex);
			}
			if (!record.Enabled)
			{
				sb.Append(" enabled = false");
			}
			sb.Append('\n');

			foreach (var comp in record.Components)
			{
				sb.Append("[component ").Append(comp.TypeName);
				if (!comp.Enabled)
				{
					sb.Append(" enabled = false");
				}
				sb.Append("]\n");

				foreach (var kv in comp.Properties)
				{
					sb.Append('\t').Append(kv.Key).Append(" = ").Append(EncodeValue(kv.Value)).Append('\n');
				}
			}
		}

		foreach (var rel in snapshot.Relations)
		{
			if (rel.SourceIndex < 0 || rel.SourceIndex >= snapshot.Objects.Count ||
				rel.TargetIndex < 0 || rel.TargetIndex >= snapshot.Objects.Count)
			{
				throw new InvalidOperationException($"快照关系索引越界（{rel.TypeName}）。");
			}
			sb.Append("relation ").Append(rel.TypeName)
				.Append(" #").Append(rel.SourceIndex)
				.Append(" -> #").Append(rel.TargetIndex)
				.Append('\n');
		}

		return sb.ToString();
	}

	/// <summary>
	/// 评审修订 §3.2：Serialize 前置快照校验——ParentIndex 合法性 + 完整 DFS 前序（祖先栈），
	/// 与 Deserialize 共用同一语义；非法快照直接抛错，禁止静默降级为顶层（防数据损坏）。
	/// </summary>
	private static void ValidateSnapshot(GameWorldSnapshot snapshot)
	{
		var ancestors = new List<int>(snapshot.Objects.Count);
		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			var record = snapshot.Objects[i];
			if (record.ParentIndex < -1 || record.ParentIndex >= i)
			{
				throw new InvalidOperationException($"Serialize 快照对象 #{i} \"{record.Name}\" 的 ParentIndex={record.ParentIndex} 非法（须 -1 或 [0, {i})，父先出现契约）。");
			}
			if (record.ParentIndex == -1)
			{
				ancestors.Clear();
			}
			else
			{
				int depth = ancestors.IndexOf(record.ParentIndex);
				if (depth < 0)
				{
					throw new InvalidOperationException($"Serialize 快照对象 #{i} \"{record.Name}\" 的父 #{record.ParentIndex} 不在开放祖先链（非 DFS 前序，快照损坏）。");
				}
				ancestors.RemoveRange(depth + 1, ancestors.Count - depth - 1);
			}
			ancestors.Add(i);
		}
	}

	/// <summary>解析文本为快照（只做语法层；语义容错在 Restore，R24）。</summary>
	public static GameWorldSnapshot Deserialize(string text)
	{
		ArgumentNullException.ThrowIfNull(text);

		var snapshot = new GameWorldSnapshot();
		int currentObject = -1;    // 当前对象在快照中的索引
		int currentComponent = -1; // 当前组件在 record.Components 中的索引
		bool sawFormat = false;     // 评审修订 §3.1：头部阶段化——首条有效行 format、次条 kind
		bool sawKind = false;
		var ancestors = new List<int>(); // 评审修订 §3.2：DFS 前序祖先栈校验
		int lineNo = 0;

		foreach (string raw in text.Split('\n'))
		{
			lineNo++;
			string line = TrimIndent(raw.TrimEnd('\r')); // 草案 §3.2：缩进无含义，仅排版
			if (line.Length == 0 || line.StartsWith('#'))
			{
				continue;
			}

			// 头部阶段化（评审修订 §3.1）。
			if (!sawFormat)
			{
				if (!line.StartsWith("format = ", StringComparison.Ordinal))
				{
					throw Error(lineNo, $"首条有效行必须是 format 头，实际：{line}");
				}
				CheckHeaderValue(line, "format", FormatName, lineNo);
				sawFormat = true;
				continue;
			}
			if (!sawKind)
			{
				if (!line.StartsWith("kind = ", StringComparison.Ordinal))
				{
					throw Error(lineNo, $"第二条有效行必须是 kind 头，实际：{line}");
				}
				CheckHeaderValue(line, "kind", KindScene, lineNo);
				sawKind = true;
				continue;
			}
			// 头部已齐：正文中出现头部行 → 报错（评审修订：重复/乱序）。
			if (line.StartsWith("format = ", StringComparison.Ordinal) || line.StartsWith("kind = ", StringComparison.Ordinal))
			{
				throw Error(lineNo, $"头部行重复或出现在正文之后：{line}");
			}

			// 对象行：object #<DFS序号> "<名字>" [parent = #<父序号>] [enabled = false]
			if (line.StartsWith("object ", StringComparison.Ordinal))
			{
				string rest = line.Substring("object ".Length).Trim();
				int hashAt = rest.IndexOf('#');
				int nameStart = hashAt >= 0 ? rest.IndexOf('"', hashAt) : -1;
				if (hashAt < 0 || nameStart < 0)
				{
					throw Error(lineNo, $"对象行格式错误：{line}");
				}
				string indexText = rest.Substring(hashAt + 1, nameStart - hashAt - 1).Trim();
				if (!int.TryParse(indexText, NumberStyles.None, CultureInfo.InvariantCulture, out int index) || index < 0)
				{
					throw Error(lineNo, $"对象序号必须是非负整数：{rest}");
				}
				int close = FindClosingQuote(rest, nameStart);
				if (close < 0)
				{
					throw Error(lineNo, $"对象名字缺少闭合引号：{rest}");
				}
				string name = UnescapeStrict(rest.Substring(nameStart + 1, close - nameStart - 1));
				string tail = rest.Substring(close + 1).Trim();
				int parentIndex = -1; // 缺省顶层；parent = #p 显式引用（草案 §3.2/§3.3）
				bool enabled = true;
				if (tail.Length > 0)
				{
					// 支持任意顺序的尾注：parent = #n 与 enabled = false。
					string remaining = tail;
					int guard = 0;
					while (remaining.Length > 0)
					{
						if (guard++ > 4)
						{
							throw Error(lineNo, $"对象行尾注无法解析：{tail}");
						}
						if (remaining.StartsWith("parent = #", StringComparison.Ordinal))
						{
							int pEnd = remaining.IndexOfAny(new[] { ' ', '\t' }, "parent = #".Length);
							string pText = pEnd < 0 ? remaining.Substring("parent = #".Length) : remaining.Substring("parent = #".Length, pEnd - "parent = #".Length);
							if (!int.TryParse(pText, NumberStyles.None, CultureInfo.InvariantCulture, out parentIndex) || parentIndex < 0)
							{
								throw Error(lineNo, $"parent 引用必须是非负序号 #n：{remaining}");
							}
							remaining = pEnd < 0 ? "" : remaining.Substring(pEnd).Trim();
						}
						else if (remaining.StartsWith("enabled = false", StringComparison.Ordinal))
						{
							enabled = false;
							remaining = remaining.Substring("enabled = false".Length).Trim();
						}
						else
						{
							throw Error(lineNo, $"对象行尾注只支持 parent = #n 与 enabled = false：{remaining}");
						}
					}
				}

				// DFS 序号连续校验（快照索引即 DFS 序）。
				if (index != snapshot.Objects.Count)
				{
					throw Error(lineNo, $"对象序号不连续：期望 #{snapshot.Objects.Count}，实际 #{index}（DFS 序契约）。");
				}

				currentObject = snapshot.Objects.Count;
				currentComponent = -1;
				// 评审修订 §3.2：DFS 前序严格校验（祖先栈），非仅 parent < 当前（防 A→A-child→B→A-grandchild 非法序列）。
				if (parentIndex == -1)
				{
					ancestors.Clear();
				}
				else
				{
					if (parentIndex < 0 || parentIndex >= currentObject)
					{
						throw Error(lineNo, $"对象 #{currentObject} \"{name}\" 的 parent = #{parentIndex} 越界（须 < 自身序号且非自身）。");
					}
					int depth = ancestors.IndexOf(parentIndex);
					if (depth < 0)
					{
						throw Error(lineNo, $"对象 #{currentObject} \"{name}\" 的 parent = #{parentIndex} 不在开放祖先链（非 DFS 前序，评审修订 §3.2）。");
					}
					ancestors.RemoveRange(depth + 1, ancestors.Count - depth - 1);
				}
				ancestors.Add(currentObject);
				snapshot.Objects.Add(new GameObjectRecord
				{
					Name = name,
					Enabled = enabled,
					ParentIndex = parentIndex,
					Components = new List<GameComponentRecord>(),
				});
				continue;
			}

			// 组件块头：[component <稳定名>] [enabled = false]
			if (line.StartsWith("[component ", StringComparison.Ordinal))
			{
				if (currentObject < 0)
				{
					throw Error(lineNo, "组件块出现在对象行之前。");
				}
				int closeBracket = line.IndexOf(']', "[component ".Length);
				if (closeBracket < 0)
				{
					throw Error(lineNo, $"组件块头缺少闭合 ]：{line}");
				}
				string content = line.Substring("[component ".Length, closeBracket - "[component ".Length).Trim();
				string typeName;
				bool enabled = true;
				int sp = content.IndexOf(' ');
				if (sp >= 0)
				{
					typeName = content.Substring(0, sp);
					string tail = content.Substring(sp).Trim();
					if (tail == "enabled = false")
					{
						enabled = false;
					}
					else
					{
						throw Error(lineNo, $"组件块头尾注只支持 enabled = false：{tail}");
					}
				}
				else
				{
					typeName = content;
				}
				if (typeName.Length == 0)
				{
					throw Error(lineNo, "组件类型名不能为空。");
				}

				var record = snapshot.Objects[currentObject];
				currentComponent = record.Components.Count;
				record.Components.Add(new GameComponentRecord { TypeName = typeName, Enabled = enabled, Properties = new List<KeyValuePair<string, object?>>() });
				continue;
			}

			// 关系行：relation <稳定名> #<源序号> -> #<目标序号>
			if (line.StartsWith("relation ", StringComparison.Ordinal))
			{
				string rest = line.Substring("relation ".Length).Trim();
				int arrow = rest.IndexOf(" -> ", StringComparison.Ordinal);
				if (arrow < 0)
				{
					throw Error(lineNo, $"关系行缺少 ' -> ' 分隔：{rest}");
				}
				string left = rest.Substring(0, arrow).Trim();
				string right = rest.Substring(arrow + 4).Trim();
				// 格式：relation <稳定名> #<源> -> #<目标>——类型名在前，序号在后。
				int srcHash = left.IndexOf('#');
				if (srcHash < 0)
				{
					throw Error(lineNo, $"关系行缺少源序号：{rest}");
				}
				string typeName = left.Substring(0, srcHash).Trim();
				string srcText = left.Substring(srcHash + 1).Trim();
				if (!int.TryParse(srcText, NumberStyles.None, CultureInfo.InvariantCulture, out int srcIndex) || srcIndex < 0)
				{
					throw Error(lineNo, $"关系行源序号必须是非负整数：{rest}");
				}
				int dstHash = right.IndexOf('#');
				if (dstHash < 0)
				{
					throw Error(lineNo, $"关系行缺少目标序号：{rest}");
				}
				string dstText = right.Substring(dstHash + 1).Trim();
				if (!int.TryParse(dstText, NumberStyles.None, CultureInfo.InvariantCulture, out int dstIndex) || dstIndex < 0)
				{
					throw Error(lineNo, $"关系行目标序号必须是非负整数：{rest}");
				}
				if (typeName.Length == 0)
				{
					throw Error(lineNo, $"关系行类型名不能为空：{rest}");
				}
				// 引用校验（草案 §3.3）：源/目标必须在已出现对象范围内（< 当前总数）。
				if (srcIndex >= snapshot.Objects.Count || dstIndex >= snapshot.Objects.Count)
				{
					throw Error(lineNo, $"关系行序号越界（对象总数 {snapshot.Objects.Count}）：{rest}");
				}
				snapshot.Relations.Add(new RelationRecord { TypeName = typeName, SourceIndex = srcIndex, TargetIndex = dstIndex });
				currentComponent = -1; // 评审修订 §3.4：关系行结束当前组件块（属性不得归属旧组件）
				continue;
			}

			// 属性行：<属性名> = <值>（必须位于 [component] 块内）。
			int eq = line.IndexOf(" = ", StringComparison.Ordinal);
			if (eq > 0)
			{
				if (currentObject < 0)
				{
					throw Error(lineNo, "属性行出现在对象行之前。");
				}
				if (currentComponent < 0)
				{
					throw Error(lineNo, $"属性行 {line} 出现在组件块之外（需先有 [component ...] 头）。");
				}
				string key = line.Substring(0, eq).Trim();
				string literal = line.Substring(eq + 3).Trim();
				if (key.Length == 0)
				{
					throw Error(lineNo, "属性名不能为空。");
				}
				var compProps = snapshot.Objects[currentObject].Components[currentComponent].Properties;
				foreach (var existing in compProps)
				{
					if (existing.Key == key)
					{
						throw Error(lineNo, $"组件内属性名重复：{key}（评审修订 §3.5 防覆盖语义歧义）。");
					}
				}
				compProps.Add(new KeyValuePair<string, object?>(key, ParseValue(literal)));
				continue;
			}

			throw Error(lineNo, $"无法识别的行：{line}");
		}

		if (!sawFormat || !sawKind)
		{
			throw new InvalidOperationException("文本缺少 format/kind 头部（契约 R26 / 评审修订 §3.1 强制阶段化）。");
		}

		// 草案 §3.3：序号即身份——ParentIndex 已在对象行解析时直接直译，无需收尾名字解析。

		return snapshot;
	}

	private static void CheckHeaderValue(string line, string key, string expected, int lineNo)
	{
		string value = line.Substring(key.Length + 3).Trim(); // "key = \"value\"" → value
		if (!value.StartsWith('"') || !value.EndsWith('"'))
		{
			throw Error(lineNo, $"{key} 头部值必须是双引号字符串：{line}");
		}
		string inner = value.Substring(1, value.Length - 2);
		if (inner != expected)
		{
			throw Error(lineNo, $"{key} 头部值不匹配：期望 \"{expected}\"，实际 \"{inner}\"。");
		}
	}

	/// <summary>草案 §3.2：缩进无含义——剥掉行首空白（仅排版）。</summary>
	private static string TrimIndent(string line)
	{
		int n = 0;
		while (n < line.Length && (line[n] == '\t' || line[n] == ' '))
		{
			n++;
		}
		return line.Substring(n);
	}

	private static int FindClosingQuote(string s, int open)
	{
		for (int i = open + 1; i < s.Length; i++)
		{
			if (s[i] == '\\')
			{
				i++;
				continue;
			}
			if (s[i] == '"')
			{
				return i;
			}
		}
		return -1;
	}

	private static InvalidOperationException Error(int lineNo, string message) =>
		new InvalidOperationException($"文本第 {lineNo} 行：{message}");

	// ---------- 值编码（自描述字面量，规格见草案 §3.5） ----------

	private static string EncodeValue(object? value)
	{
		switch (value)
		{
			case null:
				return "null";
			case bool b:
				return b ? "true" : "false";
			case int i:
				return i.ToString(CultureInfo.InvariantCulture);
			case float f:
				if (float.IsNaN(f) || float.IsInfinity(f))
				{
					throw new InvalidOperationException($"属性值 float 非有限（NaN/±Infinity）不在文本格式范围（草案 §3.5 拒绝策略）。");
				}
				return EnsureDecimalPoint(f.ToString("R", CultureInfo.InvariantCulture));
			case double d:
				if (double.IsNaN(d) || double.IsInfinity(d))
				{
					throw new InvalidOperationException($"属性值 double 非有限（NaN/±Infinity）不在文本格式范围（草案 §3.5 拒绝策略）。");
				}
				return EnsureDecimalPoint(d.ToString("R", CultureInfo.InvariantCulture));
			case string s:
				return "\"" + Escape(s) + "\"";
			case Enum e:
				// 裸词 = enum 名；无名字（未定义组合值）或名字命中保留 token / 数字形态 → 输出底层数值（评审修订 §3.5 enum 冲突策略）。
				string? name = Enum.GetName(e.GetType(), e);
				if (name != null && !IsReservedWord(name))
				{
					return name;
				}
				Type underlying = Enum.GetUnderlyingType(e.GetType());
				return underlying == typeof(ulong)
					? Convert.ToUInt64(e, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture)
					: Convert.ToInt64(e, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture);
			case LiteralToken t:
				return EncodeToken(t); // 评审修订：token 类型化输出（幂等）
			default:
				throw new InvalidOperationException($"属性值类型 {value.GetType().Name} 不在可读文本编码范围（白名单见契约 §10）。");
		}
	}

	private static string EncodeToken(LiteralToken t)
	{
		switch (t.Kind)
		{
			case LiteralKind.Null: return "null";
			case LiteralKind.Bool: return (bool)t.Value! ? "true" : "false";
			case LiteralKind.Int: return t.Lexeme;
			case LiteralKind.Float: return t.Lexeme;
			case LiteralKind.String: return "\"" + Escape((string)t.Value!) + "\"";
			case LiteralKind.Bare: return t.Lexeme;
			default: throw new InvalidOperationException($"未知 token 类别 {t.Kind}。");
		}
	}

	/// <summary>评审修订：enum 名命中保留 token / 数字形态 → 需输出底层数值（§3.5）。</summary>
	private static bool IsReservedWord(string s) =>
		s == "null" || s == "true" || s == "false" ||
		int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out _) ||
		double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out _);
	/// <summary>float/double 序列化保证带小数点（与 int 区分，草案 §3.5）。</summary>
	private static string EnsureDecimalPoint(string text) =>
		(text.Contains('.') || text.Contains('e') || text.Contains('E')) ? text : text + ".0";

	private static object? ParseValue(string literal)
	{
		// 评审修订 §3.5：token 类型化——词法语义显式保留，Restore 按目标 Schema 严格转换。
		if (literal == "null")
		{
			return new LiteralToken(LiteralKind.Null, literal, null);
		}
		if (literal == "true")
		{
			return new LiteralToken(LiteralKind.Bool, literal, true);
		}
		if (literal == "false")
		{
			return new LiteralToken(LiteralKind.Bool, literal, false);
		}
		if (literal.Length >= 2 && literal[0] == '"')
		{
			// 严格转义校验（评审修订 §3.5）：闭合引号必须完整消费（无尾垃圾）；内部转义由 UnescapeStrict 保证。
			int close = FindClosingQuote(literal, 0);
			if (close < 0 || close != literal.Length - 1)
			{
				throw new InvalidOperationException($"字符串字面量缺少闭合引号或闭合后有多余字符：{literal}");
			}
			return new LiteralToken(LiteralKind.String, literal, UnescapeStrict(literal.Substring(1, literal.Length - 2)));
		}
		if (int.TryParse(literal, NumberStyles.Integer, CultureInfo.InvariantCulture, out int i))
		{
			return new LiteralToken(LiteralKind.Int, literal, i);
		}
		if (double.TryParse(literal, NumberStyles.Float, CultureInfo.InvariantCulture, out double d) &&
			(literal.Contains('.') || literal.Contains('e') || literal.Contains('E')))
		{
			return new LiteralToken(LiteralKind.Float, literal, d);
		}
		// 裸词：enum 名或数值字符串（enum 冲突策略由 Serialize/ConvertValue 闭环，评审修订 §3.5）。
		// 裸词：enum 名或数值字符串（enum 冲突策略由 Serialize/ConvertValue 闭环，评审修订 §3.5）。
		return new LiteralToken(LiteralKind.Bare, literal, null);
	}

	private static string Escape(string s)
	{
		var sb = new StringBuilder(s.Length);
		foreach (char c in s)
		{
			switch (c)
			{
				case '\\': sb.Append("\\\\"); break;
				case '"': sb.Append("\\\""); break;
				case '\n': sb.Append("\\n"); break;
				case '\r': sb.Append("\\r"); break;
				case '\t': sb.Append("\\t"); break;
				default: sb.Append(c); break;
			}
		}
		return sb.ToString();
	}

	/// <summary>严格转义解码（评审修订 §3.5）：未知转义 / 悬空反斜杠 → 抛错（不再静默吞反斜杠）。</summary>
	private static string UnescapeStrict(string s)
	{
		var sb = new StringBuilder(s.Length);
		for (int i = 0; i < s.Length; i++)
		{
			char c = s[i];
			if (c == '\\')
			{
				if (i + 1 >= s.Length)
				{
					throw new InvalidOperationException($"字符串含悬空反斜杠：{s}");
				}
				char next = s[i + 1];
				switch (next)
				{
					case '\\': sb.Append('\\'); break;
					case '"': sb.Append('"'); break;
					case 'n': sb.Append('\n'); break;
					case 'r': sb.Append('\r'); break;
					case 't': sb.Append('\t'); break;
					default: throw new InvalidOperationException($"字符串含未知转义序列 \\{next}（仅允许 \\ \" \\n \\r \\t，评审修订 §3.5）：{s}");
				}
				i++;
				continue;
			}
			sb.Append(c);
		}
		return sb.ToString();
	}

	/// <summary>评审修订 §3.5：值 token 类型化——词法类别 + 原文 + 解析值（替代旧 BareToken 单一形态）。</summary>
	internal enum LiteralKind
	{
		Null,
		Bool,
		Int,
		Float,
		String,
		Bare,
	}

	/// <summary>
	/// token：Deserialize 保留词法语义（类别 + 原文），Serialize 按类别原样输出（幂等），
	/// Restore 按目标 Schema strict 转换矩阵（§3.5）——替代无边界 Convert.* 的静默舍入/漂移。
	/// </summary>
	internal sealed class LiteralToken
	{
		/// <summary>词法类别（Null/Bool/Int/Float/String/Bare）。</summary>
		public LiteralKind Kind { get; }

		/// <summary>字面量原文（错误信息与幂等输出用）。</summary>
		public string Lexeme { get; }

		/// <summary>解析出的值（Bare → null；由 Restore 按目标类型解析）。</summary>
		public object? Value { get; }

		internal LiteralToken(LiteralKind kind, string lexeme, object? value)
		{
			Kind = kind;
			Lexeme = lexeme;
			Value = value;
		}
	}
}
