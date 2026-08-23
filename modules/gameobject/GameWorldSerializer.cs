// SPDX-License-Identifier: MIT
// GameWorldSerializer.cs —— 确定性序列化（O1，方案 §6.4/契约 §10）
//
// 导出/重建 GameWorldSnapshot（对象记录 + 关系记录），round-trip 后 hash 相等。
// - 对象遍历序：Roots 深度优先（父先子后）⇔ 快照索引稳定；Parent 用快照索引表达。
// - 属性序：Schema SerializedProperties（[GameProperty]，声明序稳定）。
// - hash：FNV-1a 64，顺序敏感；值用 InvariantCulture 规范字符串。
// - 不含运行时 EntityId：两个内容相同的世界 hash 相同（确定性验证口径）。

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Baize.GameObject;

/// <summary>对象快照记录（序列化模型）。</summary>
public sealed class GameObjectRecord
{
	/// <summary>对象名字。</summary>
	public string Name = string.Empty;

	/// <summary>对象启用标志（契约 §3）。</summary>
	public bool Enabled = true;

	/// <summary>父对象在快照中的索引（-1 = 顶层）。</summary>
	public int ParentIndex = -1;

	/// <summary>组件记录（插入序）。</summary>
	public List<ComponentRecord> Components = new();
}

/// <summary>组件快照记录。</summary>
public sealed class ComponentRecord
{
	/// <summary>稳定类型名。</summary>
	public string TypeName = string.Empty;

	/// <summary>组件启用标志。</summary>
	public bool Enabled = true;

	/// <summary>可序列化属性（键序 = Schema 声明序，确定性）。</summary>
	public List<KeyValuePair<string, object?>> Properties = new();
}

/// <summary>关系快照记录。</summary>
public sealed class RelationRecord
{
	/// <summary>关系稳定类型名。</summary>
	public string TypeName = string.Empty;

	/// <summary>源对象快照索引。</summary>
	public int SourceIndex;

	/// <summary>目标对象快照索引。</summary>
	public int TargetIndex;
}

/// <summary>世界快照：对象（深度优先序）+ 关系（插入序）。</summary>
public sealed class GameWorldSnapshot
{
	/// <summary>对象记录。</summary>
	public List<GameObjectRecord> Objects = new();

	/// <summary>关系记录。</summary>
	public List<RelationRecord> Relations = new();
}

/// <summary>确定性序列化器：Capture / Restore / ComputeHash。</summary>
public static class GameWorldSerializer
{
	/// <summary>导出世界快照（不修改世界）。</summary>
	public static GameWorldSnapshot Capture(GameWorld world)
	{
		ArgumentNullException.ThrowIfNull(world);
		var snapshot = new GameWorldSnapshot();
		var indexByObject = new Dictionary<GameObject, int>();
		var order = new List<GameObject>();

		// 深度优先（父先子后）收集对象；快照索引即 DFS 序（order 与快照一一对应）。
		void Walk(GameObject obj)
		{
			indexByObject.Add(obj, snapshot.Objects.Count);
			order.Add(obj);
			var record = new GameObjectRecord
			{
				Name = obj.Name,
				Enabled = obj.Enabled,
			};
			var schemaCache = new Dictionary<Type, ComponentSchema>();
			foreach (var comp in world.GetComponentList(obj))
			{
				var compRecord = new ComponentRecord
				{
					// 稳定全限定名（reviewer P1：与 Schema 注册键一致，防跨命名空间同名冲突）
					TypeName = world.Schemas.Get(comp.GetType()).TypeName,
					Enabled = comp.Enabled,
				};
				Type type = comp.GetType();
				if (!schemaCache.TryGetValue(type, out var schema))
				{
					schema = world.Schemas.Get(type);
					schemaCache.Add(type, schema);
				}
				foreach (var ps in schema.SerializedProperties)
				{
					if (!ps.IsWhitelistedType)
					{
						throw new InvalidOperationException($"组件 {type.Name} 的属性 {ps.Name} 类型 {ps.ValueType.Name} 不在 O1 序列化白名单（契约 §10）。");
					}
					compRecord.Properties.Add(new KeyValuePair<string, object?>(ps.Name, ps.Info.GetValue(comp)));
				}
				record.Components.Add(compRecord);
			}
			snapshot.Objects.Add(record);

			foreach (var child in world.GetChildren(obj))
			{
				Walk(child);
			}
		}

		foreach (var root in world.Roots)
		{
			Walk(root);
		}

		// 补全 ParentIndex（访问 order 而非字典 key，确定性）。
		for (int i = 0; i < order.Count; i++)
		{
			var parent = world.GetParent(order[i]);
			if (parent != null && indexByObject.TryGetValue(parent, out int parentIndex))
			{
				snapshot.Objects[i].ParentIndex = parentIndex;
			}
		}

		// 关系：按插入序（确定性），Source/Target 用快照索引。
		foreach (var rel in world.Relations.All)
		{
			var source = world.GetObject(rel.Source);
			var target = world.GetObject(rel.Target);
			if (source == null || target == null)
			{
				continue; // 一端已销毁（正常流程不会发生，防御）
			}
			snapshot.Relations.Add(new RelationRecord
			{
				TypeName = RelationGraph.StableTypeKey(rel.GetType()), // 稳定全限定名（reviewer P1：RelationName 可被覆盖）
				SourceIndex = indexByObject[source],
				TargetIndex = indexByObject[target],
			});
		}

		return snapshot;
	}

	/// <summary>
	/// 从快照重建世界（无副作用于原世界）。组件严格按记录序创建；
	/// 组件 Enabled 先于 AddComponent 设置（保证生命周期回调符合契约 §4）；
	/// 组件/关系类型注册表从原世界（schemaSource/relationSource）复制，否则需事先在目标世界注册。
	/// </summary>
	public static GameWorld Restore(GameWorldSnapshot snapshot, ComponentSchemaRegistry? schemaSource = null, RelationGraph? relationSource = null)
	{
		ArgumentNullException.ThrowIfNull(snapshot);
		var world = new GameWorld();
		// 类型注册表从原世界复制（组件 + 关系）。
		if (schemaSource != null)
		{
			world.Schemas.CopyFrom(schemaSource);
		}
		if (relationSource != null)
		{
			world.Relations.CopyFactoriesFrom(relationSource);
		}
		if (snapshot.Objects.Count == 0)
		{
			return world;
		}

		// 先创建全部对象（深度优先序 ⇒ 父索引总小于子索引，父先建）。
		var created = new GameObject[snapshot.Objects.Count];
		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			created[i] = world.CreateGameObject(snapshot.Objects[i].Name);
			world.SetEnabled(created[i], snapshot.Objects[i].Enabled);
		}
		// 先补父子关系（AddComponent 前：保证父链 effective 已就位，
		// 避免组件先以顶层身份 enable、随后挂到禁用父下再 disable 的闪烁）。
		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			int parentIndex = snapshot.Objects[i].ParentIndex;
			if (parentIndex >= 0 && parentIndex < created.Length)
			{
				world.SetParent(created[i], created[parentIndex]);
			}
		}

		// 再逐对象加组件（保持插入序）。此时 effective 状态已含父链与暂停。
		for (int i = 0; i < snapshot.Objects.Count; i++)
		{
			var record = snapshot.Objects[i];
			foreach (var compRecord in record.Components)
			{
				if (!world.Schemas.TryGetByName(compRecord.TypeName, out var schema))
				{
					throw new InvalidOperationException($"快照包含未注册组件类型 {compRecord.TypeName}（Restore 前需先注册 Schema）。");
				}
				var comp = (GameComponent)Activator.CreateInstance(schema.ComponentType)!;
				comp.Enabled = compRecord.Enabled; // 先于 AddComponent 设置，避免过期 enable 闪烁
				foreach (var kv in compRecord.Properties)
				{
					FindProperty(schema, kv.Key).Info.SetValue(comp, ConvertValue(kv.Value, kv.Key, schema));
				}
				world.AddComponent(created[i], comp);
			}
		}

		// 最后关系（类型必须在 RelationGraph 注册过）。
		foreach (var relRecord in snapshot.Relations)
		{
			if (relRecord.SourceIndex < 0 || relRecord.SourceIndex >= created.Length ||
				relRecord.TargetIndex < 0 || relRecord.TargetIndex >= created.Length)
			{
				throw new InvalidOperationException($"快照关系索引越界（{relRecord.TypeName}）。");
			}
			world.Relations.RestoreTyped(relRecord.TypeName, created[relRecord.SourceIndex].Id, created[relRecord.TargetIndex].Id);
		}

		return world;
	}

	private static PropertySchema FindProperty(ComponentSchema schema, string name)
	{
		foreach (var ps in schema.SerializedProperties)
		{
			if (ps.Name == name)
			{
				return ps;
			}
		}
		throw new InvalidOperationException($"组件 {schema.TypeName} 缺少已序列化属性 {name}（Schema 已变更？）。");
	}

	private static object? ConvertValue(object? value, string propertyName, ComponentSchema schema)
	{
		if (value == null)
		{
			return null;
		}
		var propertySchema = FindProperty(schema, propertyName);
		Type targetType = propertySchema.ValueType;
		Type ut = Nullable.GetUnderlyingType(targetType) ?? targetType;
		if (ut.IsEnum)
		{
			return Enum.ToObject(ut, value);
		}
		if (ut == typeof(int))
		{
			return Convert.ToInt32(value, CultureInfo.InvariantCulture);
		}
		if (ut == typeof(float))
		{
			return Convert.ToSingle(value, CultureInfo.InvariantCulture);
		}
		if (ut == typeof(double))
		{
			return Convert.ToDouble(value, CultureInfo.InvariantCulture);
		}
		if (ut == typeof(bool))
		{
			return Convert.ToBoolean(value);
		}
		if (ut == typeof(string))
		{
			return Convert.ToString(value, CultureInfo.InvariantCulture);
		}
		throw new InvalidOperationException($"类型 {targetType.Name} 不在 O1 序列化白名单。");
	}

	/// <summary>确定性 hash（FNV-1a 64；顺序敏感，契约 §10）。</summary>
	public static ulong ComputeHash(GameWorldSnapshot snapshot)
	{
		ArgumentNullException.ThrowIfNull(snapshot);
		ulong hash = 14695981039346656037UL;
		var sb = new StringBuilder();

		foreach (var record in snapshot.Objects)
		{
			sb.Append('O').Append(record.Name.Length).Append(':').Append(record.Name).Append('|').Append(record.Enabled ? '1' : '0').Append('|').Append(record.ParentIndex).Append('|');
			foreach (var comp in record.Components)
			{
				sb.Append('C').Append(comp.TypeName.Length).Append(':').Append(comp.TypeName).Append('|').Append(comp.Enabled ? '1' : '0').Append('|');
				foreach (var kv in comp.Properties)
				{
					sb.Append(kv.Key).Append('=').Append(Normalize(kv.Value)).Append(';');
				}
				sb.Append('|');
			}
			sb.Append('\n');
		}
		foreach (var rel in snapshot.Relations)
		{
			sb.Append('R').Append(rel.TypeName.Length).Append(':').Append(rel.TypeName).Append('|').Append(rel.SourceIndex).Append("->").Append(rel.TargetIndex).Append('\n');
		}

		byte[] bytes = Encoding.UTF8.GetBytes(sb.ToString());
		foreach (byte b in bytes)
		{
			hash ^= b;
			hash *= 1099511628211UL;
		}
		return hash;
	}

/// <summary>
	/// 规范化值编码（reviewer P2）：带类型标签 + 长度前缀，消除 null/字符串歧义；
	/// 枚举按底层类型输出完整位模式（防 ulong 溢出），字符串含长度前缀防拼接歧义。
	/// </summary>
	private static string Normalize(object? value)
	{
		if (value == null)
		{
			return "z0;"; // null 显式标签（与字符串 "n" 区分）
		}
		if (value is bool b)
		{
			return "b" + (b ? '1' : '0') + ";";
		}
		if (value is Enum e)
		{
			return "e" + EnumBits(e) + ";";
		}
		if (value is string s)
		{
			return "s" + s.Length.ToString(CultureInfo.InvariantCulture) + ":" + s + ";";
		}
		// int/float/double 等：类型标签 + invariant 文本。
		string text = Convert.ToString(value, CultureInfo.InvariantCulture) ?? "?";
		return "v" + value.GetType().Name + ":" + text + ";";
	}

	/// <summary>枚举完整位模式（reviewer P2：不 Convert.ToInt64，防 ulong 溢出）。</summary>
	private static string EnumBits(Enum e)
	{
		Type underlying = Enum.GetUnderlyingType(e.GetType());
		ulong bits;
		if (underlying == typeof(ulong))
		{
			bits = Convert.ToUInt64(e, CultureInfo.InvariantCulture);
		}
		else
		{
			bits = unchecked((ulong)Convert.ToInt64(e, CultureInfo.InvariantCulture));
		}
		return bits.ToString("X16", CultureInfo.InvariantCulture);
	}
}
