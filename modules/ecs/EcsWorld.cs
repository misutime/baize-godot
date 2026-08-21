// SPDX-License-Identifier: MIT
// EcsWorld.cs —— baize-godot EcsWorld 框架核心（P2.1）
//
// 面向游戏开发者的 ECS 框架层：封装 Friflo 底层，
// 提供固定 Tick / 输入 / Command / 实体安全 / 系统调度（按 Phase）/ 重置。
// 游戏代码不直接调 Friflo 底层（全部经 EcsWorld）。

using System;
using System.Collections.Generic;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace Baize.Ecs;

/// <summary>
/// EcsWorld —— 游戏世界的框架层容器。
/// 持有 EntityStore + SystemRoot，提供统一的 Step(InputFrame) 入口。
/// </summary>
public sealed class EcsWorld : IDisposable
{
    private readonly EntityStore _store;
    private readonly SystemRoot _root;
    private readonly Dictionary<Phase, SystemGroup> _phaseGroups;
    private readonly WorldCommandBuffer _commandBuffer;
    private readonly WorldEvents _events;
    private readonly EcsResource _resources;

    private ulong _tickIndex;
    private bool _disposed;

    /// <summary>固定步长（秒）。</summary>
    public float FixedDelta { get; }

    /// <summary>当前 Tick 序号。</summary>
    public ulong TickIndex => _tickIndex;

    /// <summary>底层 Friflo Store（高级用途，一般不用）。</summary>
    public EntityStore Store => _store;

    /// <summary>事件总线（系统间纯数据通信，EventWriter/EventReader）。</summary>
    public WorldEvents Events => _events;

    /// <summary>全局单例资源（GameState/Score/配置，借鉴 Bevy Resource）。</summary>
    public EcsResource Resources => _resources;

    // Friflo EntitySchema 是进程级单例——AOT 注册 + CreateSchema 只执行一次
    // （P1-2 修复：静态锁内二次检查，防并发竞态）
    private static readonly object _schemaLock = new();
    private static bool _schemaCreated;

    /// <summary>创建 EcsWorld。固定步长默认 1/60 秒。</summary>
    /// <param name="registerTypes">AOT 类型注册回调（游戏项目传入 EcsAotRegistration.RegisterAll，P2-3 生成器）。</param>
    public EcsWorld(Action<NativeAOT> registerTypes, float fixedDelta = 1f / 60f)
    {
        FixedDelta = fixedDelta;

        EnsureSchemaCreated(registerTypes);   // 进程级单例，线程安全

        _store = new EntityStore();
        _root = new SystemRoot(_store);
        _phaseGroups = CreatePhaseGroups(_root);
        _commandBuffer = new WorldCommandBuffer(_store);
        _events = new WorldEvents();
        _resources = new EcsResource();
    }

    /// <summary>进程级 EntitySchema 初始化（线程安全：锁内二次检查）。</summary>
    private static void EnsureSchemaCreated(Action<NativeAOT> registerTypes)
    {
        lock (_schemaLock)
        {
            if (_schemaCreated) return;
            var aot = new NativeAOT();
            registerTypes(aot);
            aot.CreateSchema();
            _schemaCreated = true;
        }
    }

    /// <summary>按 Phase 枚举顺序建 SystemGroup，挂到 SystemRoot（P1-1 修复：真正按阶段调度）。</summary>
    private static Dictionary<Phase, SystemGroup> CreatePhaseGroups(SystemRoot root)
    {
        var groups = new Dictionary<Phase, SystemGroup>();
        foreach (Phase phase in Enum.GetValues<Phase>())
        {
            var group = new SystemGroup(phase.ToString());
            groups[phase] = group;
            root.Add(group);   // SystemGroup : BaseSystem，按枚举顺序添加
        }
        return groups;
    }

    /// <summary>推进一个固定 Tick：按 Phase 顺序跑系统（Input → ... → RenderExtract）。</summary>
    public void Step(in InputFrame input)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(EcsWorld));

        // 1. 存输入（系统可读 CurrentInput）
        CurrentInput = input;

        // 2. 事件推进：pending → events（系统可读本 Tick 事件）
        _events.Flush();

        // 3. 播放上 Tick 累积的延迟结构变更
        _commandBuffer.Playback();

        // 4. 更新所有系统（固定步长，按 Phase 组顺序）
        _root.Update(new UpdateTick(FixedDelta, _tickIndex * FixedDelta));

        // 5. Tick 递增
        _tickIndex++;
    }

    /// <summary>当前 Tick 的输入（系统读取）。</summary>
    public InputFrame CurrentInput { get; private set; }

    /// <summary>注册一个系统到指定阶段（P1-1 修复：挂到对应 SystemGroup，执行顺序由枚举保证）。</summary>
    public void AddSystem(BaseSystem system, Phase phase = Phase.Simulation)
    {
        _phaseGroups[phase].Add(system);
    }

    /// <summary>获取 CommandBuffer（延迟结构变更：创建/删除/添加组件）。</summary>
    public WorldCommandBuffer CommandBuffer => _commandBuffer;

    /// <summary>重置世界：清空实体、Tick、命令、事件、系统状态（保留 Resources 配置——由调用方重建游戏状态）。</summary>
    public void Reset()
    {
        // 删除所有实体
        foreach (var entity in _store.Entities)
        {
            entity.DeleteEntity();
        }
        _tickIndex = 0;
        _commandBuffer.Reset();
        _events.Reset();

        // P1-3：重置有状态的系统（实现 IResettableSystem 的）
        foreach (var group in _phaseGroups.Values)
        {
            ResetSystemsInGroup(group);
        }
    }

    private static void ResetSystemsInGroup(SystemGroup group)
    {
        foreach (var system in group)
        {
            if (system is IResettableSystem resettable)
            {
                resettable.ResetState();
            }
            if (system is SystemGroup nested)
            {
                ResetSystemsInGroup(nested);
            }
        }
    }

    /// <summary>释放资源。</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
    }
}

/// <summary>
/// 可重置系统接口（P1-3 修复）：有累计状态（计时器/随机/缓存）的系统实现它，
/// EcsWorld.Reset 时调用 ResetState 恢复初始。
/// </summary>
public interface IResettableSystem
{
    /// <summary>重置系统内部状态（Step→Reset→Step 后从初始状态开始）。</summary>
    void ResetState();
}

/// <summary>阶段分组（系统执行顺序）。</summary>
public enum Phase
{
    Input = 0,
    Spawn,
    Simulation,
    Collision,
    Resolve,
    Cleanup,
    RenderExtract,
}
