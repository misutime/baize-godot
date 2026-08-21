// SPDX-License-Identifier: MIT
// EcsWorld.cs —— baize-godot EcsWorld 框架核心（P2.1）
//
// 面向游戏开发者的 ECS 框架层：封装 Friflo 底层，
// 提供固定 Tick / 输入 / Command / 实体安全 / 系统调度 / 重置。
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
    private readonly PhaseGroup _phases;
    private readonly WorldCommandBuffer _commandBuffer;
    private readonly WorldEvents _events;

    private ulong _tickIndex;
    private bool _disposed;

    /// <summary>固定步长（秒）。</summary>
    public float FixedDelta { get; }

    /// <summary>当前 Tick 序号。</summary>
    public ulong TickIndex => _tickIndex;

    /// <summary>底层 Friflo Store（高级用途，一般不用）。</summary>
    public EntityStore Store => _store;

    /// <summary>事件总线（系统间纯数据通信）。</summary>
    public WorldEvents Events => _events;

    /// <summary>创建 EcsWorld。固定步长默认 1/60 秒。</summary>
    /// <param name="registerTypes">AOT 类型注册回调（游戏项目传入 EcsAotRegistration.RegisterAll，P2-3 生成器）。</param>
    // Friflo EntitySchema 是进程级单例——AOT 注册 + CreateSchema 只执行一次
    private static bool _schemaCreated;

    public EcsWorld(Action<NativeAOT> registerTypes, float fixedDelta = 1f / 60f)
    {
        FixedDelta = fixedDelta;

        if (!_schemaCreated)
        {
            // AOT 注册（游戏项目提供的生成器代码），仅首次
            var aot = new NativeAOT();
            registerTypes(aot);
            aot.CreateSchema();
            _schemaCreated = true;
        }

        _store = new EntityStore();
        _root = new SystemRoot(_store);
        _phases = new PhaseGroup();
        _commandBuffer = new WorldCommandBuffer(_store);
        _events = new WorldEvents();
    }

    /// <summary>推进一个固定 Tick：跑所有阶段系统（Input → ... → RenderExtract）。</summary>
    public void Step(in InputFrame input)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(EcsWorld));

        // 1. 存输入（系统可读 CurrentInput）
        CurrentInput = input;

        // 2. 事件推进：pending → events（系统可读本 Tick 事件）
        _events.Flush();

        // 3. 播放上 Tick 累积的延迟结构变更
        _commandBuffer.Playback();

        // 3. 更新所有系统（固定步长）
        _root.Update(new UpdateTick(FixedDelta, _tickIndex * FixedDelta));

        // 4. Tick 递增
        _tickIndex++;
    }

    /// <summary>当前 Tick 的输入（系统读取）。</summary>
    public InputFrame CurrentInput { get; private set; }

    /// <summary>注册一个系统到指定阶段。</summary>
    public void AddSystem(BaseSystem system, Phase phase = Phase.Simulation)
    {
        _phases.Add(system, phase);
        _root.Add(system);
    }

    /// <summary>获取 CommandBuffer（延迟结构变更：创建/删除/添加组件）。</summary>
    public WorldCommandBuffer CommandBuffer => _commandBuffer;

    /// <summary>重置世界：清空所有实体与系统状态。</summary>
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
    }

    /// <summary>释放资源。</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
    }
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

/// <summary>阶段分组管理器：把系统按阶段组织。</summary>
internal sealed class PhaseGroup
{
    private readonly Dictionary<Phase, List<BaseSystem>> _systems = new();

    public void Add(BaseSystem system, Phase phase)
    {
        if (!_systems.TryGetValue(phase, out var list))
        {
            list = new List<BaseSystem>();
            _systems[phase] = list;
        }
        list.Add(system);
    }
}


