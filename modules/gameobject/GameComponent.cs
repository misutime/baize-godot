// SPDX-License-Identifier: MIT
// GameComponent.cs —— 组件基类与生命周期（O1，方案 §4.4 / 契约 §4）
//
// 普通组件有生命周期 + C# 行为（不是纯数据）；ECS/SoA 只是可选性能后端（§14.2）。
// 生命周期顺序见 O1 契约 §4：OnCreate → OnEnable → OnStart → OnTick/OnFixedTick → OnDisable → OnDestroy。

namespace Baize.GameObject;

/// <summary>
/// GameComponent —— GameObject 的能力/状态单元。
/// 派生类必须无参可构造（世界通过 Activator 创建）；属性可选标 [GameProperty] 参与确定性序列化。
/// </summary>
public abstract class GameComponent
{
	private GameObject? _owner;
	private bool _enabled = true;

	/// <summary>所属 GameObject（未加入世界时为 null）。由世界管理，勿自行赋值。</summary>
	public GameObject? Owner => _owner;

	/// <summary>世界引用（未加入世界时为 null）。</summary>
	public GameWorld? World => _owner?.World;

	/// <summary>组件启用标志（独立于 GameObject.Enabled，契约 §3）。</summary>
	public bool Enabled
	{
		get => _enabled;
		set
		{
			if (_enabled == value)
			{
				return;
			}
			_enabled = value;
			// 有效状态翻转由世界统一刷新（父链/暂停参与 effective 计算）。
			if (_owner != null && !_owner.IsDestroyed)
			{
				_owner.World.RefreshEffective(_owner);
			}
		}
	}

	/// <summary>组件修订号：每次加入世界时递增（调试/工具用）。</summary>
	public uint Revision { get; internal set; }

	/// <summary>是否已调用过 OnStart（只在第一次有效 tick 前调用一次，契约 §4）。</summary>
	internal bool Started { get; set; }

	/// <summary>当前"有效激活"状态（effective enabled 且组件 Enabled），仅翻转时触发 OnEnable/OnDisable。</summary>
	internal bool EffectiveActive { get; set; }

	internal void AttachTo(GameObject owner) => _owner = owner;

	internal void Detach() => _owner = null;

	/// <summary>组件被加入世界（对象已存在）时调用；无论 enabled 与否（"已注册"，契约 §4）。</summary>
	public virtual void OnCreate()
	{
	}

	/// <summary>组件进入"有效激活"时调用（对象有效 + 组件 Enabled；父链/暂停参与）。</summary>
	public virtual void OnEnable()
	{
	}

	/// <summary>组件第一次有效 tick 前调用一次（契约 §4；OnEnable 之后、本帧 OnTick 之前）。</summary>
	public virtual void OnStart()
	{
	}

	/// <summary>每 variable tick 调用（delta = 本帧秒）。</summary>
	public virtual void OnTick(float delta)
	{
	}

	/// <summary>每 fixed tick 调用（delta = world.FixedDelta）。</summary>
	public virtual void OnFixedTick(float delta)
	{
	}

	/// <summary>组件离开"有效激活"时调用（对象/父链禁用、暂停、或组件 Enabled=false）。</summary>
	public virtual void OnDisable()
	{
	}

	/// <summary>组件被移除或 owner 销毁时调用（契约 §4/§5，同步）。</summary>
	public virtual void OnDestroy()
	{
	}
}
