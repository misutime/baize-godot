// SPDX-License-Identifier: MIT
// EcsState.cs —— 世界级声明式状态与进入/退出生命周期

using System;
using System.Collections.Generic;

namespace Baize.Ecs;

/// <summary>非泛型状态标记，供 System 声明运行条件与获取状态资源。</summary>
public interface IEcsState
{
	/// <summary>当前状态值（仅用于通用框架检查）。</summary>
	object CurrentValue { get; }

	/// <summary>当前状态是否等于给定值。</summary>
	bool Is(object value);
}

internal interface IEcsStateBinding
{
	void Bind(EcsWorld world);
}

/// <summary>
/// 世界级声明式状态机。状态本身是 Resource；长期玩法数据仍放 Resource/Component，
/// 转换副作用集中在 OnExit/OnEnter，而不是散落到每个 System 的 if 分支。
/// </summary>
public abstract class EcsState<TState> : IEcsState, IEcsStateBinding
	where TState : struct, Enum
{
	private EcsWorld? _world;

	protected EcsState(TState initialState)
	{
		Current = initialState;
	}

	/// <summary>当前状态；只能通过 TransitionTo 改变。</summary>
	public TState Current { get; private set; }

	object IEcsState.CurrentValue => Current;

	/// <summary>当前是否处于指定状态。</summary>
	public bool Is(TState state) => EqualityComparer<TState>.Default.Equals(Current, state);

	bool IEcsState.Is(object value) => value is TState state && Is(state);

	/// <summary>按 OnExit(旧状态) → 切换 → OnEnter(新状态) 的顺序执行转换。</summary>
	public void TransitionTo(TState next)
	{
		if (_world is null)
		{
			throw new InvalidOperationException(
				$"状态 {GetType().Name} 必须先通过 EcsWorld.InsertResource 安装，才能执行转换。");
		}

		if (Is(next)) return;

		TState previous = Current;
		OnExit(_world, previous);
		Current = next;
		OnEnter(_world, next);
	}

	void IEcsStateBinding.Bind(EcsWorld world)
	{
		if (ReferenceEquals(_world, world)) return;
		if (_world is not null)
		{
			throw new InvalidOperationException(
				$"状态 {GetType().Name} 已绑定到另一个 EcsWorld。");
		}

		_world = world;
		try
		{
			OnEnter(world, Current);
		}
		catch
		{
			// P2-2：OnEnter 失败后解绑（_world 恢复 null），否则重试 InsertResource 会因
			// ReferenceEquals 直接返回而永久跳过初始 OnEnter。
			_world = null;
			throw;
		}
	}

	/// <summary>离开某状态时调用。默认无操作。</summary>
	protected virtual void OnExit(EcsWorld world, TState state) { }

	/// <summary>进入某状态时调用；首次 InsertResource 也会进入初始状态。默认无操作。</summary>
	protected virtual void OnEnter(EcsWorld world, TState state) { }
}
