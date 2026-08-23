// SPDX-License-Identifier: MIT
// BSceneLoader.cs —— .bscene/.bprefab 加载器（O4，O4-bscene-bprefab格式与实例化.md）
//
// 链路：文件文本 → Deserialize（场景语法）→ 展开 prefab 实例 + override 区 → 完整快照 → Restore 建世界。
// - prefabResolver：路径 → prefab 文本（测试注入内存字典；O5 文件系统接入后换资源加载器）。
// - 遵循 O4 文档 §4/§5：模板深拷贝 + @id 重映射 + SourceTemplate 记录 + override 应用。
// - 确定性：DFS 前序/序号连续由 Deserialize 与展开共同保证；未知组件/属性走 R24（Restore 层）。

using System;
using System.Collections.Generic;

namespace Baize.GameObject;

/// <summary>
/// .bscene/.bprefab 加载器（O4）：文件 → 快照 → 世界。
/// 格式权威：O3-可读格式契约草案.md（编码）+ O4-bscene-bprefab格式与实例化.md（文件层扩展）。
/// </summary>
public static class BSceneLoader
{
	/// <summary>加载场景文本为完整快照（含 prefab 实例化展开 + override 应用；不含 Restore）。</summary>
	public static GameWorldSnapshot LoadScene(
		string text,
		Func<string, string?>? prefabResolver = null)
	{
		ArgumentNullException.ThrowIfNull(text);
		// [override] 区属于文件层（BSceneLoader 消费），先剥离再走 O3 Deserialize（O4 §4.3）。
		string scenePart = text;
		int overrideMarker = text.IndexOf("[override]", StringComparison.Ordinal);
		if (overrideMarker >= 0)
		{
			scenePart = text.Substring(0, overrideMarker);
		}
		var snapshot = GameWorldTextSerializer.Deserialize(scenePart);

		// 展开 prefab 实例（对象级 prefab = "路径" 字段 → SourceTemplate）。
		ExpandPrefabs(snapshot, prefabResolver);

		// 应用 override 区（文件尾部 [override] 标记）。
		ApplyOverrides(snapshot, text);
		return snapshot;
	}

	/// <summary>场景文本 → 世界（复用 Restore 与 R24 容错）。</summary>
	public static GameWorld LoadSceneToWorld(
		string text,
		ComponentSchemaRegistry schemas,
		RelationGraph? relations = null,
		Func<string, string?>? prefabResolver = null,
		RestoreOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(schemas);
		var snapshot = LoadScene(text, prefabResolver);
		return GameWorldSerializer.Restore(snapshot, options ?? new RestoreOptions(), schemas, relations);
	}

	/// <summary>解析 prefab 模板文本为快照（不展开实例；语义同场景子集）。</summary>
	public static GameWorldSnapshot ParsePrefab(string text)
	{
		ArgumentNullException.ThrowIfNull(text);
		// O4 §4.1：prefab 文件 kind 必须为 "prefab"（Deserialize 层只确认存在性，这里做语义校验）。
		foreach (string raw in text.Split('\n'))
		{
			string t = raw.Trim();
			if (t.StartsWith("kind = ", StringComparison.Ordinal))
			{
				if (!t.Contains("prefab"))
				{
					throw new InvalidOperationException("prefab 文件 kind 必须为 \"prefab\"（O4 §4.1）。");
				}
				break;
			}
		}
		var snapshot = GameWorldTextSerializer.Deserialize(text);
		if (snapshot.Objects.Count == 0)
		{
			throw new InvalidOperationException("prefab 模板必须含至少一个 root 对象（O4 §4.1）。");
		}
		return snapshot;
	}

	// ---------- prefab 实例化 ----------

	private static void ExpandPrefabs(GameWorldSnapshot snapshot, Func<string, string?>? resolver)
	{
		if (resolver == null)
		{
			return; // 无解析器：保持原状（调用方自行处理 SourceTemplate 字段）
		}

		// 逐对象检查 prefab 字段（存放在 SourceTemplate；当前 Deserialize 未解析该字段——见下方回退）。
		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			var record = snapshot.Objects[i];
			if (record.SourceTemplate.Length == 0)
			{
				continue;
			}
			string path = record.SourceTemplate;
			string? prefabText = resolver(path);
			if (prefabText == null)
			{
				throw new InvalidOperationException($"prefab 引用无法解析：{path}（prefabResolver 返回 null）。");
			}
			var template = ParsePrefab(prefabText);
			ExpandPrefabs(template, resolver); // 递归：模板内嵌套 prefab
			InstantiateAt(snapshot, i, template);
		}
	}

	/// <summary>把模板树实例化到 snapshot 中，替换对象 rootIndex（保留其身份/名称/层级位置/SourceTemplate）。</summary>
	private static void InstantiateAt(GameWorldSnapshot snapshot, int rootIndex, GameWorldSnapshot template)
	{
		int templateCount = template.Objects.Count;
		int parentIndex = snapshot.Objects[rootIndex].ParentIndex;
		string name = snapshot.Objects[rootIndex].Name;
		bool enabled = snapshot.Objects[rootIndex].Enabled;
		ulong authoringId = snapshot.Objects[rootIndex].AuthoringId;
		string source = snapshot.Objects[rootIndex].SourceTemplate;

		// 1) 深拷贝模板记录（属性值装箱引用可共享——值类型/string 安全）。
		var copied = new GameObjectRecord[templateCount];
		for (int t = 0; t < templateCount; t++)
		{
			var src = template.Objects[t];
			var dst = new GameObjectRecord
			{
				Name = src.Name,
				Enabled = src.Enabled,
				ParentIndex = src.ParentIndex,
				AuthoringId = src.AuthoringId,
				SourceTemplate = src.SourceTemplate,
			};
			dst.Components = new List<GameComponentRecord>(src.Components.Count);
			foreach (var c in src.Components)
			{
				var cc = new GameComponentRecord
				{
					TypeName = c.TypeName,
					Enabled = c.Enabled,
					Properties = new List<KeyValuePair<string, object?>>(c.Properties),
				};
				dst.Components.Add(cc);
			}
			copied[t] = dst;
		}

		// 2) root 继承场景声明；其余节点 ParentIndex 整体右移 rootIndex（模板 DFS 序 ⇒ 插入到 rootIndex 起连续块）。
		copied[0].Name = name;
		copied[0].Enabled = enabled;
		copied[0].AuthoringId = authoringId;
		copied[0].SourceTemplate = source;
		copied[0].ParentIndex = parentIndex;
		for (int t = 1; t < templateCount; t++)
		{
			if (copied[t].ParentIndex >= 0)
			{
				copied[t].ParentIndex = copied[t].ParentIndex + rootIndex; // 模板内父索引 → 全局索引
			}
		}

		// 3) 替换：原 root 位置换成模板树（root 处于 rootIndex，子树紧随其后——DFS 序连续）。
		var newObjects = new List<GameObjectRecord>(snapshot.Objects.Count + templateCount - 1);
		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			if (i == rootIndex)
			{
				for (int t = 0; t < templateCount; t++)
				{
					newObjects.Add(copied[t]);
				}
			}
			else
			{
				// 原 rootIndex 之后的节点序号整体后移 (templateCount - 1)，父引用同步。
				int shift = i > rootIndex ? templateCount - 1 : 0;
				var r = snapshot.Objects[i];
				newObjects.Add(r);
				if (shift > 0 && r.ParentIndex > rootIndex)
				{
					r.ParentIndex += shift;
				}
			}
		}
		snapshot.Objects.Clear();
		snapshot.Objects.AddRange(newObjects);

		// 4) 关系端点重映射：引用了原 rootIndex 及之后节点的关系，序号平移。
		foreach (var rel in snapshot.Relations)
		{
			if (rel.SourceIndex > rootIndex)
			{
				rel.SourceIndex += templateCount - 1;
			}
			if (rel.TargetIndex > rootIndex)
			{
				rel.TargetIndex += templateCount - 1;
			}
		}
	}

	// ---------- override 区 ----------

	/// <summary>解析文件尾部 [override] 区并应用到快照（O4 §4.3；未知组件/属性留给 Restore R24）。</summary>
	private static void ApplyOverrides(GameWorldSnapshot snapshot, string text)
	{
		// override 区格式（O4 §4.3）：
		//   [override]
		//       <对象引用> <组件稳定名> <属性名> = <值 token>
		string marker = "[override]";
		int idx = text.IndexOf(marker, StringComparison.Ordinal);
		if (idx < 0)
		{
			return;
		}
		string section = text.Substring(idx + marker.Length);
		foreach (string rawLine in section.Split('\n'))
		{
			string line = rawLine.Trim();
			if (line.Length == 0 || line.StartsWith('#'))
			{
				continue;
			}
			ApplyOverrideLine(snapshot, line);
		}
	}

	private static void ApplyOverrideLine(GameWorldSnapshot snapshot, string line)
	{
		// 对象引用：@id 或 #序号（首个 token，含 @/# 前缀）。
		int sp = line.IndexOf(' ');
		if (sp < 0)
		{
			throw new InvalidOperationException($"override 行格式错误（期望 <对象引用> <组件> <属性> = <值>）：{line}");
		}
		string objRef = line.Substring(0, sp);
		string rest = line.Substring(sp).Trim();
		// 组件名与属性名：组件 <稳定名> 之后是 <属性> = <值>；用最后一个 ' = ' 分隔属性。
		// 组件名与属性名：格式 <组件稳定名>.<属性名>（O4 §4.3）；组件名含命名空间点 → 从最后一个 '.' 切分。
		int eq = rest.LastIndexOf(" = ", StringComparison.Ordinal);
		if (eq < 0)
		{
			throw new InvalidOperationException($"override 行缺少 ' = '（期望 <组件稳定名>.<属性名> = <值>）：{line}");
		}
		string compProp = rest.Substring(0, eq).Trim();
		string literal = rest.Substring(eq + 3).Trim();
		int dot = compProp.LastIndexOf('.');
		if (dot <= 0 || dot == compProp.Length - 1)
		{
			throw new InvalidOperationException($"override 行缺少组件名与属性名分隔（<组件稳定名>.<属性名>）：{line}");
		}
		string compType = compProp.Substring(0, dot).Trim();
		string propName = compProp.Substring(dot + 1).Trim();

		// 定位对象。
		int objIndex = ResolveObjectRef(objRef, snapshot);
		// 定位组件（同类型多实例：取第一个）。
		var record = snapshot.Objects[objIndex];
		GameComponentRecord? target = null;
		foreach (var comp in record.Components)
		{
			if (comp.TypeName == compType)
			{
				target = comp;
				break;
			}
		}
		if (target == null)
		{
			// 未知组件（模板/场景都无）→ 抛错（与 Restore 无关；R24 语义在 Restore 层，但 override 未知组件通常作者笔误）。
			throw new InvalidOperationException($"override 引用组件不存在：{compType}（对象 {objRef}）。");
		}
		// 写入属性（追加；重复属性校验留给后续 Restore strict 路径——这里允许覆盖场景值）。
		// 覆盖语义：若属性已存在则替换值（override 覆盖模板/场景默认）。
		for (int i = 0; i < target.Properties.Count; i++)
		{
			if (target.Properties[i].Key == propName)
			{
				target.Properties[i] = new KeyValuePair<string, object?>(propName, GameWorldTextSerializer.ParseValueForOverride(literal));
				return;
			}
		}
		target.Properties.Add(new KeyValuePair<string, object?>(propName, GameWorldTextSerializer.ParseValueForOverride(literal)));
	}

	private static int ResolveObjectRef(string objRef, GameWorldSnapshot snapshot)
	{
		if (objRef.StartsWith('@'))
		{
			if (!ulong.TryParse(objRef.Substring(1), System.Globalization.NumberStyles.HexNumber, null, out ulong id))
			{
				throw new InvalidOperationException($"override 对象引用格式错误：{objRef}");
			}
			for (int i = 0; i < snapshot.Objects.Count; i++)
			{
				if (snapshot.Objects[i].AuthoringId == id)
				{
					return i;
				}
			}
			throw new InvalidOperationException($"override 引用不存在的作者ID：@{objRef.Substring(1)}");
		}
		if (objRef.StartsWith('#'))
		{
			if (int.TryParse(objRef.Substring(1), out int idx) && idx >= 0 && idx < snapshot.Objects.Count)
			{
				return idx;
			}
			throw new InvalidOperationException($"override 对象序号越界：# {objRef.Substring(1)}");
		}
		throw new InvalidOperationException($"override 对象引用必须 @id 或 #序号：{objRef}");
	}
}
