// SPDX-License-Identifier: MIT
// EcsSystem.cs —— 依赖显式、无世界字段样板的作者层 System 基类

using System;
using System.Collections.Generic;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace Baize.Ecs;

internal interface IEcsSystemBinding
{
	void Bind(EcsWorld world);
}

internal interface IEcsSystemCondition
{
	bool Matches(EcsWorld world);
}

internal sealed class EcsStateCondition<TState> : IEcsSystemCondition
	where TState : class, IEcsState
{
	private readonly object _required;

	public EcsStateCondition(object required)
	{
		// P1-3：校验 required 是枚举（状态值是枚举）；Is(object) 内再做与 Current 的精确类型匹配。
		if (required is null || !required.GetType().IsEnum)
		{
			throw new ArgumentException(
				$"RunInState<{typeof(TState).Name}> 的 required 必须是该状态的枚举值（收到 " +
				$"{required?.GetType().Name ?? "null"}）。", nameof(required));
		}
		_required = required;
	}

	public bool Matches(EcsWorld world)
	{
		TState state = world.GetState<TState>();
		// P1-1：校验 required 的枚举类型与状态 CurrentValue 类型一致，否则抛异常——
		// 避免传错枚举（如 MatchState.SomeOtherEnum）时 Is(object) 静默返回 false 停用规则。
		object current = state.CurrentValue;
		if (current.GetType() != _required.GetType())
		{
			throw new InvalidOperationException(
				$"RunInState<{typeof(TState).Name}> 的 required 类型 {_required.GetType().Name} " +
				$"与状态值类型 {current.GetType().Name} 不匹配。");
		}
		return state.Is(_required);
	}
}

internal sealed class EcsSystemContext
{
	private readonly List<IEcsSystemCondition> _conditions = new();
	private EcsWorld? _world;

	public EcsWorld World => _world ?? throw new InvalidOperationException(
		"EcsSystem 必须先通过 EcsWorld.AddSystem 注册，才能运行。");

	public void Bind(EcsWorld world)
	{
		if (ReferenceEquals(_world, world)) return;
		if (_world is not null)
		{
			throw new InvalidOperationException("同一个 EcsSystem 实例不能属于多个世界。");
		}
		_world = world;
	}

	public void RunInState<TState>(object required) where TState : class, IEcsState =>
		_conditions.Add(new EcsStateCondition<TState>(required));
	public EcsReadQuery<T1> Read<T1>() where T1 : struct, IComponent => new(World.Store);
	public EcsReadQuery<T1, T2> Read<T1, T2>()
		where T1 : struct, IComponent where T2 : struct, IComponent => new(World.Store);
	public EcsReadQuery<T1, T2, T3> Read<T1, T2, T3>()
		where T1 : struct, IComponent where T2 : struct, IComponent
		where T3 : struct, IComponent => new(World.Store);
	public EcsReadQuery<T1, T2, T3, T4> Read<T1, T2, T3, T4>()
		where T1 : struct, IComponent where T2 : struct, IComponent
		where T3 : struct, IComponent where T4 : struct, IComponent => new(World.Store);
	public EcsReadQuery<T1, T2, T3, T4, T5> Read<T1, T2, T3, T4, T5>()
		where T1 : struct, IComponent where T2 : struct, IComponent
		where T3 : struct, IComponent where T4 : struct, IComponent
		where T5 : struct, IComponent => new(World.Store);

	public bool ShouldRun()
	{
		EcsWorld world = World;
		foreach (IEcsSystemCondition condition in _conditions)
		{
			if (!condition.Matches(world)) return false;
		}
		return true;
	}
}

/// <summary>
/// 不查询固定组件组合的轻量 System 基类。
/// System 应只保存安装期配置或每次 Execute 都清空的临时工作区；
/// 会影响玩法、存档、回放或 Reset 的状态必须放入 Resource/Component。
/// </summary>
public abstract class EcsSystem : BaseSystem, IEcsSystemBinding
{
	private readonly EcsSystemContext _context = new();

	protected EcsWorld World => _context.World;
	protected InputFrame Input => World.CurrentInput;
	protected T Res<T>() where T : class => World.GetState<T>();
	protected T State<T>() where T : class, IEcsState => World.GetState<T>();
	protected EventReader<T> ReadEvents<T>() where T : struct => World.Events.Reader<T>();
	protected EventWriter<T> WriteEvents<T>() where T : struct => World.Events.Writer<T>();
	protected EcsReadQuery<T1> Read<T1>() where T1 : struct, IComponent => _context.Read<T1>();
	protected EcsReadQuery<T1, T2> Read<T1, T2>()
		where T1 : struct, IComponent where T2 : struct, IComponent => _context.Read<T1, T2>();
	protected EcsReadQuery<T1, T2, T3> Read<T1, T2, T3>()
		where T1 : struct, IComponent where T2 : struct, IComponent
		where T3 : struct, IComponent => _context.Read<T1, T2, T3>();
	protected EcsReadQuery<T1, T2, T3, T4> Read<T1, T2, T3, T4>()
		where T1 : struct, IComponent where T2 : struct, IComponent
		where T3 : struct, IComponent where T4 : struct, IComponent => _context.Read<T1, T2, T3, T4>();
	protected EcsReadQuery<T1, T2, T3, T4, T5> Read<T1, T2, T3, T4, T5>()
		where T1 : struct, IComponent where T2 : struct, IComponent
		where T3 : struct, IComponent where T4 : struct, IComponent
		where T5 : struct, IComponent => _context.Read<T1, T2, T3, T4, T5>();
	protected void RunInState<TState>(object required) where TState : class, IEcsState =>
		_context.RunInState<TState>(required);

	void IEcsSystemBinding.Bind(EcsWorld world) => _context.Bind(world);

	protected sealed override void OnUpdateGroup()
	{
		if (_context.ShouldRun()) Execute();
	}

	protected abstract void Execute();
}

/// <summary>带一个组件查询的作者层 System；底层仍使用 Friflo QuerySystem。</summary>
public abstract class EcsSystem<T1> : QuerySystem<T1>, IEcsSystemBinding
	where T1 : struct, IComponent
{
	private readonly EcsSystemContext _context = new();
	protected EcsWorld World => _context.World;
	protected InputFrame Input => World.CurrentInput;
	protected T Res<T>() where T : class => World.GetState<T>();
	protected T State<T>() where T : class, IEcsState => World.GetState<T>();
	protected EventReader<T> ReadEvents<T>() where T : struct => World.Events.Reader<T>();
	protected EventWriter<T> WriteEvents<T>() where T : struct => World.Events.Writer<T>();
	protected void RunInState<TState>(object required) where TState : class, IEcsState => _context.RunInState<TState>(required);
	protected void ForTag<TTag>() where TTag : struct, ITag => Filter.AllTags(Tags.Get<TTag>());
	protected void ForEach(ForEachEntity<T1> action) => Query.ForEachEntity(action);
	protected EcsReadQuery<TRead1> Read<TRead1>() where TRead1 : struct, IComponent => _context.Read<TRead1>();
	protected EcsReadQuery<TRead1, TRead2> Read<TRead1, TRead2>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent => _context.Read<TRead1, TRead2>();
	protected EcsReadQuery<TRead1, TRead2, TRead3> Read<TRead1, TRead2, TRead3>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4> Read<TRead1, TRead2, TRead3, TRead4>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4, TRead5> Read<TRead1, TRead2, TRead3, TRead4, TRead5>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent
		where TRead5 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4, TRead5>();
	void IEcsSystemBinding.Bind(EcsWorld world) => _context.Bind(world);
	protected sealed override void OnUpdate() { if (_context.ShouldRun()) Execute(); }
	protected abstract void Execute();
}

/// <summary>带两个组件查询的作者层 System；底层仍使用 Friflo QuerySystem。</summary>
public abstract class EcsSystem<T1, T2> : QuerySystem<T1, T2>, IEcsSystemBinding
	where T1 : struct, IComponent
	where T2 : struct, IComponent
{
	private readonly EcsSystemContext _context = new();
	protected EcsWorld World => _context.World;
	protected InputFrame Input => World.CurrentInput;
	protected T Res<T>() where T : class => World.GetState<T>();
	protected T State<T>() where T : class, IEcsState => World.GetState<T>();
	protected EventReader<T> ReadEvents<T>() where T : struct => World.Events.Reader<T>();
	protected EventWriter<T> WriteEvents<T>() where T : struct => World.Events.Writer<T>();
	protected void RunInState<TState>(object required) where TState : class, IEcsState => _context.RunInState<TState>(required);
	protected void ForTag<TTag>() where TTag : struct, ITag => Filter.AllTags(Tags.Get<TTag>());
	protected void ForEach(ForEachEntity<T1, T2> action) => Query.ForEachEntity(action);
	protected EcsReadQuery<TRead1> Read<TRead1>() where TRead1 : struct, IComponent => _context.Read<TRead1>();
	protected EcsReadQuery<TRead1, TRead2> Read<TRead1, TRead2>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent => _context.Read<TRead1, TRead2>();
	protected EcsReadQuery<TRead1, TRead2, TRead3> Read<TRead1, TRead2, TRead3>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4> Read<TRead1, TRead2, TRead3, TRead4>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4, TRead5> Read<TRead1, TRead2, TRead3, TRead4, TRead5>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent
		where TRead5 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4, TRead5>();
	void IEcsSystemBinding.Bind(EcsWorld world) => _context.Bind(world);
	protected sealed override void OnUpdate() { if (_context.ShouldRun()) Execute(); }
	protected abstract void Execute();
}

/// <summary>带三个组件查询的作者层 System；底层仍使用 Friflo QuerySystem。</summary>
public abstract class EcsSystem<T1, T2, T3> : QuerySystem<T1, T2, T3>, IEcsSystemBinding
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
{
	private readonly EcsSystemContext _context = new();
	protected EcsWorld World => _context.World;
	protected InputFrame Input => World.CurrentInput;
	protected T Res<T>() where T : class => World.GetState<T>();
	protected T State<T>() where T : class, IEcsState => World.GetState<T>();
	protected EventReader<T> ReadEvents<T>() where T : struct => World.Events.Reader<T>();
	protected EventWriter<T> WriteEvents<T>() where T : struct => World.Events.Writer<T>();
	protected void RunInState<TState>(object required) where TState : class, IEcsState => _context.RunInState<TState>(required);
	protected void ForTag<TTag>() where TTag : struct, ITag => Filter.AllTags(Tags.Get<TTag>());
	protected void ForEach(ForEachEntity<T1, T2, T3> action) => Query.ForEachEntity(action);
	protected EcsReadQuery<TRead1> Read<TRead1>() where TRead1 : struct, IComponent => _context.Read<TRead1>();
	protected EcsReadQuery<TRead1, TRead2> Read<TRead1, TRead2>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent => _context.Read<TRead1, TRead2>();
	protected EcsReadQuery<TRead1, TRead2, TRead3> Read<TRead1, TRead2, TRead3>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4> Read<TRead1, TRead2, TRead3, TRead4>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4, TRead5> Read<TRead1, TRead2, TRead3, TRead4, TRead5>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent
		where TRead5 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4, TRead5>();
	void IEcsSystemBinding.Bind(EcsWorld world) => _context.Bind(world);
	protected sealed override void OnUpdate() { if (_context.ShouldRun()) Execute(); }
	protected abstract void Execute();
}

/// <summary>带四个组件查询的作者层 System；底层仍使用 Friflo QuerySystem。</summary>
public abstract class EcsSystem<T1, T2, T3, T4> : QuerySystem<T1, T2, T3, T4>, IEcsSystemBinding
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
	where T4 : struct, IComponent
{
	private readonly EcsSystemContext _context = new();
	protected EcsWorld World => _context.World;
	protected InputFrame Input => World.CurrentInput;
	protected T Res<T>() where T : class => World.GetState<T>();
	protected T State<T>() where T : class, IEcsState => World.GetState<T>();
	protected EventReader<T> ReadEvents<T>() where T : struct => World.Events.Reader<T>();
	protected EventWriter<T> WriteEvents<T>() where T : struct => World.Events.Writer<T>();
	protected void RunInState<TState>(object required) where TState : class, IEcsState => _context.RunInState<TState>(required);
	protected void ForTag<TTag>() where TTag : struct, ITag => Filter.AllTags(Tags.Get<TTag>());
	protected void ForEach(ForEachEntity<T1, T2, T3, T4> action) => Query.ForEachEntity(action);
	protected EcsReadQuery<TRead1> Read<TRead1>() where TRead1 : struct, IComponent => _context.Read<TRead1>();
	protected EcsReadQuery<TRead1, TRead2> Read<TRead1, TRead2>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent => _context.Read<TRead1, TRead2>();
	protected EcsReadQuery<TRead1, TRead2, TRead3> Read<TRead1, TRead2, TRead3>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4> Read<TRead1, TRead2, TRead3, TRead4>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4, TRead5> Read<TRead1, TRead2, TRead3, TRead4, TRead5>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent
		where TRead5 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4, TRead5>();
	void IEcsSystemBinding.Bind(EcsWorld world) => _context.Bind(world);
	protected sealed override void OnUpdate() { if (_context.ShouldRun()) Execute(); }
	protected abstract void Execute();
}

/// <summary>带五个组件查询的作者层 System；底层仍使用 Friflo QuerySystem。</summary>
public abstract class EcsSystem<T1, T2, T3, T4, T5> : QuerySystem<T1, T2, T3, T4, T5>, IEcsSystemBinding
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
	where T4 : struct, IComponent
	where T5 : struct, IComponent
{
	private readonly EcsSystemContext _context = new();
	protected EcsWorld World => _context.World;
	protected InputFrame Input => World.CurrentInput;
	protected T Res<T>() where T : class => World.GetState<T>();
	protected T State<T>() where T : class, IEcsState => World.GetState<T>();
	protected EventReader<T> ReadEvents<T>() where T : struct => World.Events.Reader<T>();
	protected EventWriter<T> WriteEvents<T>() where T : struct => World.Events.Writer<T>();
	protected void RunInState<TState>(object required) where TState : class, IEcsState => _context.RunInState<TState>(required);
	protected void ForTag<TTag>() where TTag : struct, ITag => Filter.AllTags(Tags.Get<TTag>());
	protected void ForEach(ForEachEntity<T1, T2, T3, T4, T5> action) => Query.ForEachEntity(action);
	protected EcsReadQuery<TRead1> Read<TRead1>() where TRead1 : struct, IComponent => _context.Read<TRead1>();
	protected EcsReadQuery<TRead1, TRead2> Read<TRead1, TRead2>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent => _context.Read<TRead1, TRead2>();
	protected EcsReadQuery<TRead1, TRead2, TRead3> Read<TRead1, TRead2, TRead3>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4> Read<TRead1, TRead2, TRead3, TRead4>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4>();
	protected EcsReadQuery<TRead1, TRead2, TRead3, TRead4, TRead5> Read<TRead1, TRead2, TRead3, TRead4, TRead5>()
		where TRead1 : struct, IComponent where TRead2 : struct, IComponent
		where TRead3 : struct, IComponent where TRead4 : struct, IComponent
		where TRead5 : struct, IComponent => _context.Read<TRead1, TRead2, TRead3, TRead4, TRead5>();
	void IEcsSystemBinding.Bind(EcsWorld world) => _context.Bind(world);
	protected sealed override void OnUpdate() { if (_context.ShouldRun()) Execute(); }
	protected abstract void Execute();
}

