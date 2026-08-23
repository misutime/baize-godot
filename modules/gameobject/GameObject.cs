// SPDX-License-Identifier: MIT
// GameObject.cs —— 运行时游戏对象（O1，方案 §4.3 / §14.1 / 契约 §6-7）
//
// GameObject 是面向开发者的运行时游戏对象；自身不承载具体能力，添加组件后才获得能力（§14.1）。
// 结构操作全部走 World（注册/层级/生命周期同步）；本类只做校验与便捷转发。

using System;
using System.Collections.Generic;

namespace Baize.GameObject;

/// <summary>运行时游戏对象（开发者对象）。</summary>
public sealed class GameObject
{
	private readonly GameWorld _world;
	private string _name = string.Empty;

	internal GameObject(GameWorld world, ObjectId id, string name)
	{
		_world = world;
		Id = id;
		_name = name;
	}

	public ObjectId Id { get; }
	public uint CreationIndex { get; internal set; }
	/// <summary>文件层稳定身份（O4：.bscene/.bprefab 内 @hex）；0 = 运行时无文件层身份。</summary>
	public StableObjectId StableId { get; internal set; }

	/// <summary>prefab 来源模板引用（O4，契约 §8 预留）：实例对象指向 .bprefab 路径；非实例 = 空。</summary>
	public string SourceTemplate { get; internal set; } = string.Empty;
	/// <summary>创建该对象时的世界 Tick（对象创建时由世界赋值；游戏层可读，用于 O2 回滚本帧创建）。</summary>
	public ulong CreatedAtTickIndex { get; set; }
	/// <summary>逻辑事务对象 ID（0 = 非事务创建；>0 = 跨事务重映射）。</summary>
	internal long TransactionId { get; set; }
	public GameWorld World => _world;

	public string Name
	{
		get => _name;
		set => _name = value ?? string.Empty;
	}

	public bool IsDestroyed => !_world.IsAlive(Id);

	public bool Enabled
	{
		get => _world.GetEnabled(this);
		set => _world.SetEnabled(this, value);
	}

	public IReadOnlyList<GameComponent> Components => _world.GetComponentList(this);
	public GameObject? Parent => _world.GetParent(this);
	public IReadOnlyList<GameObject> Children => _world.GetChildren(this);
	public RelationAccess Relations => new(_world, this);

	public T AddComponent<T>() where T : GameComponent, new()
	{
		var component = new T();
		_world.AddComponent(this, component);
		return component;
	}

	public T AddComponent<T>(T component) where T : GameComponent
	{
		ArgumentNullException.ThrowIfNull(component);
		_world.AddComponent(this, component);
		return component;
	}

	public T? GetComponent<T>() where T : GameComponent => _world.GetComponent<T>(this);
	public IReadOnlyList<T> GetComponents<T>() where T : GameComponent => _world.GetComponents<T>(this);
	public bool RemoveComponent<T>() where T : GameComponent => _world.RemoveComponent<T>(this);

	public bool RemoveComponent(GameComponent component)
	{
		ArgumentNullException.ThrowIfNull(component);
		return _world.RemoveComponent(this, component);
	}

	public void SetParent(GameObject? newParent) => _world.SetParent(this, newParent);
	public void Destroy() => _world.Destroy(this);
	public override string ToString() => $"GameObject({Id}, \"{Name}\")";
}
