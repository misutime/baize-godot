// SPDX-License-Identifier: MIT
// Ports.cs —— Port 三通道（O5，O5-GameWorldHost与ServerPorts.md §3）
//
// §14.6 权威矩阵落地：Event / Command / Observation —— Backend 与 Gameplay 的唯一通话通道。
// 原则：Backend 永不隐式修改 Gameplay 状态；只经通道回传。

using System;
using System.Collections.Generic;

namespace Sola3d.Host;

/// <summary>事件负载基类（Backend → Gameplay：碰撞、命中、UI 点击——"发生了什么"）。</summary>
public abstract record GameplayEvent
{
	/// <summary>发生时的世界 Tick（fixed 边界，确定性对齐）。</summary>
	public ulong AtTickIndex { get; init; }
}

/// <summary>命令负载基类（Gameplay → Backend：画 Mesh、注册 Collider——"请做什么"）。</summary>
public abstract record BackendCommand
{
}

/// <summary>观察负载基类（Backend → Gameplay：Physics 权威位姿回传——"权威在那边"）。</summary>
public abstract record BackendObservation
{
	/// <summary>观察对应的世界 Tick（fixed 边界）。</summary>
	public ulong AtTickIndex { get; init; }
}

/// <summary>事件通道：Backend → Gameplay 队列。GameWorld 在 tick 边界消费。</summary>
public sealed class EventBus
{
	private readonly List<GameplayEvent> _pending = new();

	public int Count => _pending.Count;

	/// <summary>Backend 发布事件（入队，等 Gameplay 消费）。</summary>
	public void Publish(GameplayEvent e)
	{
		ArgumentNullException.ThrowIfNull(e);
		_pending.Add(e);
	}

	/// <summary>Gameplay 在 fixed tick 边界消费全部事件（消费后清空，确定性）。</summary>
	public IReadOnlyList<GameplayEvent> Drain()
	{
		var snapshot = new List<GameplayEvent>(_pending);
		_pending.Clear();
		return snapshot;
	}
}

/// <summary>命令通道：Gameplay → Backend 队列。Backend 在帧末消费。</summary>
public sealed class CommandBus
{
	private readonly List<BackendCommand> _pending = new();

	public int Count => _pending.Count;

	/// <summary>Gameplay 下发命令（入队，等 Backend 消费）。</summary>
	public void Push(BackendCommand c)
	{
		ArgumentNullException.ThrowIfNull(c);
		_pending.Add(c);
	}

	/// <summary>Backend 在帧末消费全部命令（消费后清空）。</summary>
	public IReadOnlyList<BackendCommand> Drain()
	{
		var snapshot = new List<BackendCommand>(_pending);
		_pending.Clear();
		return snapshot;
	}
}

/// <summary>观察通道：Backend → Gameplay，fixed 边界统一分发（§14.6：GameWorld 在 fixed tick 边界收集）。</summary>
public sealed class ObservationBus
{
	private readonly List<BackendObservation> _pending = new();
	private readonly List<Action<BackendObservation>> _subscribers = new();

	public int Count => _pending.Count;

	/// <summary>Backend 提交观察（入队）。</summary>
	public void Submit(BackendObservation o)
	{
		ArgumentNullException.ThrowIfNull(o);
		_pending.Add(o);
	}

	/// <summary>订阅观察（Gameplay 侧注册消费者）。</summary>
	public void Subscribe(Action<BackendObservation> handler)
	{
		ArgumentNullException.ThrowIfNull(handler);
		_subscribers.Add(handler);
	}

	/// <summary>本帧统一分发（Sola3dMainLoop.Frame 调用；消费后清空）。</summary>
	public void Dispatch()
	{
		if (_pending.Count == 0)
		{
			return;
		}
		var snapshot = new List<BackendObservation>(_pending);
		_pending.Clear();
		foreach (var observer in snapshot)
		{
			foreach (var sub in _subscribers)
			{
				sub(observer);
			}
		}
	}
}
