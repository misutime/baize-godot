// SPDX-License-Identifier: MIT
// ComponentSchema.cs —— 组件元数据（O1，方案 §8.2/§14.2 / 契约 §2/§10）
//
// 反射注册组件类型：稳定 TypeName、单/多实例、依赖、可序列化属性白名单。
// 供：AddComponent 校验、确定性序列化、后续编辑器 Components 面板与 Schema 生成器。

using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;

namespace Sola3d.GameObject;

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

	/// <summary>Inspector 显示名（缺省 = 类名，契约 R23）。</summary>
	public string? DisplayName { get; set; }

	/// <summary>Inspector 分组（缺省空串，契约 R23）。</summary>
	public string? Group { get; set; }
}

/// <summary>
/// 标记组件属性参与确定性序列化/编辑器 Inspector（O1 白名单类型见 <see cref="PropertySchema.IsWhitelistedType"/>；O3 契约 R22/R23）。
/// </summary>
[AttributeUsage(AttributeTargets.Property)]
public sealed class GamePropertyAttribute : Attribute
{
	/// <summary>Inspector 显示名（缺省 = 属性名，契约 R23）。</summary>
	public string? DisplayName { get; set; }

	/// <summary>Inspector 分组（缺省空串，契约 R23）。</summary>
	public string? Group { get; set; }

	/// <summary>Inspector 只读标记（编辑器禁编辑；序列化/恢复不受影响，契约 R23）。</summary>
	public bool ReadOnly { get; set; }

	/// <summary>默认值（编辑器「重置」与新建组件初始化用；非序列化值，契约 R23）。</summary>
	public object? DefaultValue { get; set; }
}

/// <summary>可序列化属性的元数据（序列化 + Inspector 共用，契约 R23）。</summary>
public sealed class PropertySchema
{
	private readonly Func<GameComponent, object?> _getter;
	private readonly Action<GameComponent, object?> _setter;

	/// <summary>属性名称（序列化/编辑器用，稳定）。</summary>
	public string Name { get; }

	/// <summary>属性反射句柄。</summary>
	public PropertyInfo Info { get; }

	/// <summary>属性值类型。</summary>
	public Type ValueType { get; }

	/// <summary>Inspector 显示名（缺省 = 属性名，契约 R23）。</summary>
	public string DisplayName { get; }

	/// <summary>Inspector 分组（缺省空串，契约 R23）。</summary>
	public string Group { get; }

	/// <summary>Inspector 只读标记（编辑器禁编辑，契约 R23）。</summary>
	public bool IsReadOnly { get; }

	/// <summary>默认值（编辑器「重置」/新建组件初始化用；未显式标记为 null，契约 R23）。</summary>
	public object? DefaultValue { get; }

	internal PropertySchema(PropertyInfo info, GamePropertyAttribute? attr)
	{
		Info = info;
		Name = info.Name;
		ValueType = info.PropertyType;
		DisplayName = attr?.DisplayName ?? info.Name;
		Group = attr?.Group ?? string.Empty;
		IsReadOnly = attr?.ReadOnly ?? false;
		DefaultValue = attr?.DefaultValue;
		// 契约 R22：注册时编译 get/set 委托（一次性开销），后续访问零反射。
		Type componentType = info.DeclaringType!;
		_getter = CompileGetter(componentType, info);
		_setter = CompileSetter(componentType, info);
	}

	/// <summary>Schema 驱动取属性值（契约 R22；调用方负责传入兼容组件实例）。</summary>
	public object? GetValue(GameComponent component) => _getter(component);

	/// <summary>Schema 驱动写属性值（契约 R22；调用方负责传入兼容组件实例）。</summary>
	public void SetValue(GameComponent component, object? value) => _setter(component, value);

	/// <summary>O1 序列化白名单（契约 §10）：int/float/double/bool/string 及可空同族、enum（按底层值）。</summary>
	public bool IsWhitelistedType => IsWhitelisted(ValueType);

	private static Func<GameComponent, object?> CompileGetter(Type componentType, PropertyInfo info)
	{
		var param = Expression.Parameter(typeof(GameComponent), "component");
		var typed = Expression.Convert(param, componentType);
		var body = Expression.Convert(Expression.Property(typed, info), typeof(object));
		return Expression.Lambda<Func<GameComponent, object?>>(body, param).Compile();
	}

	private static Action<GameComponent, object?> CompileSetter(Type componentType, PropertyInfo info)
	{
		var param = Expression.Parameter(typeof(GameComponent), "component");
		var typed = Expression.Convert(param, componentType);
		var value = Expression.Parameter(typeof(object), "value");
		var body = Expression.Assign(Expression.Property(typed, info), Expression.Convert(value, info.PropertyType));
		return Expression.Lambda<Action<GameComponent, object?>>(body, param, value).Compile();
	}

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
			var propAttr = pi.GetCustomAttribute<GamePropertyAttribute>();
			if (propAttr != null)
			{
				// 契约 R22：标记 [GameProperty] 的属性必须同时可读可写，否则注册报错（防作者误用）。
				if (pi.GetGetMethod() == null || pi.GetSetMethod() == null)
				{
					throw new ArgumentException($"组件 {type.FullName} 的属性 {pi.Name} 标记 [GameProperty] 但缺少可读/可写访问器（契约 R22）。");
				}
				properties.Add(new PropertySchema(pi, propAttr));
			}
		}
		var schema = new ComponentSchema(type, allowMultiple, requires, properties, attr);
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

	/// <summary>按稳定名创建组件实例（契约 R25）；未注册时报错并列出已注册类型数。</summary>
	public GameComponent CreateInstance(string typeName)
	{
		if (!_byName.TryGetValue(typeName, out var schema))
		{
			throw new InvalidOperationException($"未注册组件类型 {typeName}（已注册 {_byType.Count} 个）。");
		}
		return schema.CreateInstance();
	}
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
/// 组件 Schema：类型元数据（稳定 TypeName / 单实例 / 依赖 / 可序列化属性 + Inspector 描述）。不可变。
/// </summary>
public sealed class ComponentSchema
{
	private readonly Dictionary<string, PropertySchema> _byPropertyName;

	/// <summary>组件 CLR 类型。</summary>
	public Type ComponentType { get; }

	/// <summary>稳定类型名（序列化/反序列化双向查找用）。</summary>
	public string TypeName => ComponentType.FullName ?? ComponentType.Name; // 稳定全限定名（防跨命名空间同名冲突）

	/// <summary>Inspector 显示名（缺省 = 类名，契约 R23）。</summary>
	public string DisplayName { get; }

	/// <summary>Inspector 分组（缺省空串，契约 R23）。</summary>
	public string Group { get; }

	/// <summary>是否允许同类型多实例（契约 §1）。</summary>
	public bool AllowMultiple { get; }

	/// <summary>必需依赖组件类型（契约 §2）。</summary>
	public IReadOnlyList<Type> Requires { get; }

	/// <summary>可序列化属性（[GameProperty]，插入序 = 声明序）。</summary>
	public IReadOnlyList<PropertySchema> SerializedProperties { get; }

	internal ComponentSchema(Type type, bool allowMultiple, Type[] requires, List<PropertySchema> properties, GameComponentAttribute? attr)
	{
		ComponentType = type;
		AllowMultiple = allowMultiple;
		Requires = requires;
		SerializedProperties = properties;
		DisplayName = attr?.DisplayName ?? type.Name;
		Group = attr?.Group ?? string.Empty;
		_byPropertyName = new Dictionary<string, PropertySchema>(properties.Count);
		foreach (var ps in properties)
		{
			_byPropertyName.Add(ps.Name, ps);
		}
	}

	/// <summary>按属性名取 Schema（O(1) 索引，契约 R22）。</summary>
	public bool TryGetProperty(string name, out PropertySchema property) => _byPropertyName.TryGetValue(name, out property!);

	/// <summary>创建组件实例（无参构造；Restore 与编辑器共用，契约 R25）。</summary>
	public GameComponent CreateInstance() => (GameComponent)Activator.CreateInstance(ComponentType)!;
}
