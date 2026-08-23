// SPDX-License-Identifier: MIT
// ComponentSchema.cs —— 组件元数据（O1，方案 §8.2/§14.2 / 契约 §2/§10）
//
// 反射注册组件类型：稳定 TypeName、单/多实例、依赖、可序列化属性白名单。
// 供：AddComponent 校验、确定性序列化、后续编辑器 Components 面板与 Schema 生成器。

using System;
using System.Collections.Generic;
using System.Reflection;

namespace Baize.GameObject;

/// <summary>
/// 标记组件类型。可选配置：
/// - AllowMultiple：同类型多实例（默认 false = 单实例，契约 §1）
/// - Requires：必需依赖类型（添加时校验，契约 §2）
/// </summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class GameComponentAttribute : Attribute
{
	/// <summary>是否允许同类型多实例（默认 false）。</summary>
	public bool AllowMultiple { get; set; }

	/// <summary>必需依赖组件类型清单。</summary>
	public Type[] Requires { get; set; } = Array.Empty<Type>();
}

/// <summary>
/// 标记组件属性参与确定性序列化/编辑器 Inspector（O1 白名单类型见 <see cref="PropertySchema.IsWhitelistedType"/>）。
/// </summary>
[AttributeUsage(AttributeTargets.Property)]
public sealed class GamePropertyAttribute : Attribute
{
}

/// <summary>可序列化属性的元数据。</summary>
public sealed class PropertySchema
{
	/// <summary>属性名称（序列化/编辑器用，稳定）。</summary>
	public string Name { get; }

	/// <summary>属性反射句柄。</summary>
	public PropertyInfo Info { get; }

	/// <summary>属性值类型。</summary>
	public Type ValueType { get; }

	internal PropertySchema(PropertyInfo info)
	{
		Info = info;
		Name = info.Name;
		ValueType = info.PropertyType;
	}

	/// <summary>O1 序列化白名单（契约 §10）：int/float/double/bool/string 及可空同族、enum（按底层值）。</summary>
	public bool IsWhitelistedType => IsWhitelisted(ValueType);

	internal static bool IsWhitelisted(Type t)
	{
		Type ut = Nullable.GetUnderlyingType(t) ?? t;
		if (ut.IsEnum)
		{
			return true;
		}
		return ut == typeof(int) || ut == typeof(float) || ut == typeof(double) || ut == typeof(bool) || ut == typeof(string);
	}
}

/// <summary>组件 Schema 注册表：按类型/稳定名双向查询（世界内单例）。</summary>
public sealed class ComponentSchemaRegistry
{
	private readonly Dictionary<Type, ComponentSchema> _byType = new();
private readonly Dictionary<string, ComponentSchema> _byName = new();

	/// <summary>已注册的组件类型数。</summary>
	public int Count => _byType.Count;

	/// <summary>注册组件类型（反射构建 Schema）。重复注册同类型为幂等。</summary>
	public ComponentSchema Register(Type type)
	{
		ArgumentNullException.ThrowIfNull(type);
		if (!typeof(GameComponent).IsAssignableFrom(type))
		{
			throw new ArgumentException($"类型 {type.FullName} 不是 GameComponent，无法注册组件 Schema。");
		}
		if (_byType.TryGetValue(type, out var existing))
		{
			return existing;
		}

		var attr = type.GetCustomAttribute<GameComponentAttribute>();
		bool allowMultiple = attr?.AllowMultiple ?? false;
		Type[] requires = attr?.Requires ?? Array.Empty<Type>();

		var properties = new List<PropertySchema>();
		foreach (PropertyInfo pi in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
		{
			if (pi.GetCustomAttribute<GamePropertyAttribute>() != null)
			{
				properties.Add(new PropertySchema(pi));
			}
		}

		var schema = new ComponentSchema(type, allowMultiple, requires, properties);
		// 原子注册：先校验稳定名冲突（防半注册状态，reviewer P1），再双索引写入。
		if (_byName.TryGetValue(schema.TypeName, out var nameOwner) && nameOwner.ComponentType != type)
		{
			throw new ArgumentException($"组件稳定名 {schema.TypeName} 已被 {nameOwner.ComponentType.FullName} 占用（reviewer P1 防半注册）。");
		}
		_byType.Add(type, schema);
		_byName.Add(schema.TypeName, schema);
		return schema;
	}

	/// <summary>注册组件类型（泛型便捷入口）。</summary>
	public ComponentSchema Register<T>() where T : GameComponent => Register(typeof(T));

	/// <summary>按类型取 Schema（未注册则自动注册，保证运行时零配置可用）。</summary>
	public ComponentSchema Get(Type type)
	{
		ArgumentNullException.ThrowIfNull(type);
		if (!_byType.TryGetValue(type, out var schema))
		{
			return Register(type);
		}
		return schema;
	}

	/// <summary>按类型取 Schema（泛型便捷入口）。</summary>
	public ComponentSchema Get<T>() where T : GameComponent => Get(typeof(T));

	/// <summary>按稳定名取 Schema。</summary>
	public bool TryGetByName(string typeName, out ComponentSchema schema) => _byName.TryGetValue(typeName, out schema!);

	/// <summary>复制另一注册表的全部已注册类型（Restore 重建世界用；本表已存在的类型跳过）。</summary>
	internal void CopyFrom(ComponentSchemaRegistry other)
	{
		foreach (var type in other._byType.Keys)
		{
			if (!_byType.ContainsKey(type))
			{
				Register(type);
			}
		}
	}

	/// <summary>已注册类型集合（内部遍历用）。</summary>
	internal IEnumerable<Type> RegisteredTypes => _byType.Keys;
}

/// <summary>
/// 组件 Schema：类型元数据（稳定 TypeName / 单实例 / 依赖 / 可序列化属性）。不可变。
/// </summary>
public sealed class ComponentSchema
{
	/// <summary>组件 CLR 类型。</summary>
	public Type ComponentType { get; }

	/// <summary>稳定类型名（序列化/反序列化双向查找用）。</summary>
	public string TypeName => ComponentType.FullName ?? ComponentType.Name; // 稳定全限定名（防跨命名空间同名冲突）

	/// <summary>是否允许同类型多实例（契约 §1）。</summary>
	public bool AllowMultiple { get; }

	/// <summary>必需依赖组件类型（契约 §2）。</summary>
	public IReadOnlyList<Type> Requires { get; }

	/// <summary>可序列化属性（[GameProperty]，插入序 = 声明序）。</summary>
	public IReadOnlyList<PropertySchema> SerializedProperties { get; }

	internal ComponentSchema(Type type, bool allowMultiple, Type[] requires, List<PropertySchema> properties)
	{
		ComponentType = type;
		AllowMultiple = allowMultiple;
		Requires = requires;
		SerializedProperties = properties;
	}
}
