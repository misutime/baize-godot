// SPDX-License-Identifier: MIT
// GameObject.cs —— 开发者 Entity（O1，方案 §4.3 / §14.1 / 契约 §6-7）
//
// GameObject 是面向开发者的运行时游戏对象；自身不承载具体能力，添加组件后才获得能力（§14.1）。
// 结构操作全部走 World（注册/层级/生命周期同步）；本类只做校验与便捷转发。

using System;
using System.Collections.Generic;

namespace Baize.GameObject;

/// <summary>运行时游戏对象（开发者 Entity）。</summary>
public sealed class GameObject
{
	private readonly GameWorld _world;
	private string _name = string.Empty;

	internal GameObject(GameWorld world, EntityId id, string name)
	{
		_world = world;
		Id = id;
		_name = name;
	}

	/// <summary>运行时身份（Index + Generation，防复用，契约 §6）。</summary>
	public EntityId Id { get; }

	/// <summary>对象创建序号（世界内单调递增；tick 顺序 = 对象创建序 → 组件插入序，契约 §4）。</summary>
/// <summary>对象创建序号（世界内单调递增；tick 顺序 = 对象创建序 → 组件插入序，契约 §4）。</summary>
	public uint CreationIndex { get; internal set; }

	/// <summary>逻辑事务对象 ID（0 = 非事务创建；>0 = 事务创建，跨事务经 GameWorld 重映射解析，reviewer P1 第三轮）。</summary>
	internal long TransactionId { get; set; }
	/// <summary>所属世界。</summary>
	public GameWorld World => _world;

	/// <summary>对象名（仅作人类可读标识，不参与身份）。</summary>
	public string Name
	{
		get => _name;
		set => _name = value ?? string.Empty;
	}

	/// <summary>作者/静态场景稳定 ID（O1 占位，O4 起由 .bscene/.bprefab 解析器填充，契约 §8）。</summary>
	public AuthoringObjectId AuthoringId { get; internal set; }

	/// <summary>是否已销毁（读操作安全返回 null/false，结构操作抛异常，契约 §6）。</summary>
	public bool IsDestroyed => !_world.IsAlive(Id);

	/// <summary>对象启用标志（独立于组件 Enabled；父链/暂停参与 effective，契约 §3）。</summary>
	public bool Enabled
	{
		get => _world.GetEnabled(this);
		set => _world.SetEnabled(this, value);
	}

	/// <summary>组件容器（只读遍历）。</summary>
	public IReadOnlyList<GameComponent> Components => _world.GetComponentList(this);

	/// <summary>父对象（顶层为 null）。</summary>
	public GameObject? Parent => _world.GetParent(this);

	/// <summary>子对象列表（插入序）。</summary>
	public IReadOnlyList<GameObject> Children => _world.GetChildren(this);

	/// <summary>关系门面（以本对象为 Source 查询/添加 Relation，契约 §9）。</summary>
	public RelationAccess Relations => new(_world, this);

	/// <summary>添加组件（无参构造）；立即 OnCreate/OnEnable，OnStart 首个有效 tick（契约 §4/§5）。</summary>
	public T AddComponent<T>() where T : GameComponent, new()
	{
		var component = new T();
		_world.AddComponent(this, component);
		return component;
	}

	/// <summary>添加已构造组件（保留外部引用）。</summary>
	public T AddComponent<T>(T component) where T : GameComponent
	{
		ArgumentNullException.ThrowIfNull(component);
		_world.AddComponent(this, component);
		return component;
	}

	/// <summary>取单实例组件；不存在返回 null（读安全，契约 §6）。</summary>
	public T? GetComponent<T>() where T : GameComponent
	{
		return _world.GetComponent<T>(this);
	}

	/// <summary>取全部组件（多实例；单实例时最多一个）。</summary>
	public IReadOnlyList<T> GetComponents<T>() where T : GameComponent
	{
		return _world.GetComponents<T>(this);
	}

	/// <summary>移除单实例组件；多实例时移除第一个。返回是否移除。</summary>
	public bool RemoveComponent<T>() where T : GameComponent
	{
		return _world.RemoveComponent<T>(this);
	}

	/// <summary>移除指定组件实例（多实例精确移除）。</summary>
	public bool RemoveComponent(GameComponent component)
	{
		ArgumentNullException.ThrowIfNull(component);
		return _world.RemoveComponent(this, component);
	}

	/// <summary>重新挂到新父对象（null = 顶层；禁止环，契约 §7）。</summary>
	public void SetParent(GameObject? newParent)
	{
		_world.SetParent(this, newParent);
	}

	/// <summary>销毁本对象（同步；级联销毁子树，句柄立即失效，契约 §5/§6）。</summary>
	public void Destroy()
	{
		_world.Destroy(this);
	}

	public override string ToString() => $"GameObject({Id}, \"{Name}\")";
}
