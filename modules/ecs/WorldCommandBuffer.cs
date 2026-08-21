// SPDX-License-Identifier: MIT
// WorldCommandBuffer.cs —— baize-godot EcsWorld 命令缓冲（P2.1）
//
// 延迟结构变更：系统查询循环内禁止直接 CreateEntity/DeleteEntity/AddComponent
// （Friflo 抛 StructuralChangeException），统一走 CommandBuffer，在 Tick 末尾 Playback。
// Friflo 的 CommandBuffer 是一次性的（Playback 后需 ReturnBuffer 归还池）。
// P2-2：线程安全（锁内记录命令，并行系统可用）；Playback 仅主线程。

using Friflo.Engine.ECS;

namespace Baize.Ecs;

/// <summary>
/// 世界命令缓冲：延迟执行结构变更（创建/删除实体、添加组件/标签）。
/// 用法：系统内调用 CreateEntity() 返回 EntityCommand，链式 Add 组件，Tick 末尾 Playback 统一执行。
/// </summary>
public sealed class WorldCommandBuffer
{
    private readonly EntityStore _store;
    private readonly object _lock = new();   // P2-2：线程安全命令记录
    private Friflo.Engine.ECS.CommandBuffer? _buffer;

    internal WorldCommandBuffer(EntityStore store)
    {
        _store = store;
    }

    private Friflo.Engine.ECS.CommandBuffer Buffer
    {
        get
        {
            lock (_lock)
            {
                return _buffer ??= _store.GetCommandBuffer();
            }
        }
    }

    /// <summary>创建实体（延迟到 Playback 实际创建），返回链式命令。</summary>
    public EntityCommand CreateEntity()
    {
        lock (_lock)
        {
            int entityId = Buffer.CreateEntity();   // Friflo 预留 id
            return new EntityCommand(this, entityId);
        }
    }

    /// <summary>删除实体（延迟）。</summary>
    public void DeleteEntity(int entityId)
    {
        lock (_lock) { Buffer.DeleteEntity(entityId); }
    }

    /// <summary>给实体添加组件（延迟）。</summary>
    public void AddComponent<T>(int entityId, in T component) where T : struct, IComponent
    {
        lock (_lock) { Buffer.AddComponent(entityId, component); }
    }

    /// <summary>给实体设置组件（延迟，覆盖已有）。</summary>
    public void SetComponent<T>(int entityId, in T component) where T : struct, IComponent
    {
        lock (_lock) { Buffer.SetComponent(entityId, component); }
    }

    /// <summary>给实体添加标签（延迟）。</summary>
    public void AddTag<T>(int entityId) where T : struct, ITag
    {
        lock (_lock) { Buffer.AddTag<T>(entityId); }
    }

    /// <summary>播放所有命令（Tick 末尾由 EcsWorld 调用，**仅主线程**），随后归还缓冲供下 Tick 复用。</summary>
    public void Playback()
    {
        lock (_lock)
        {
            if (_buffer == null) return;   // 本 Tick 无命令
            _buffer.Playback();
            _buffer.ReturnBuffer();        // 归还池（Friflo 一次性语义）
            _buffer = null;
        }
    }

    /// <summary>清空缓冲并归还（Reset 用，P2-1 修复：Clear 后 ReturnBuffer 避免池泄漏）。</summary>
    public void Reset()
    {
        lock (_lock)
        {
            if (_buffer != null)
            {
                _buffer.Clear();
                _buffer.ReturnBuffer();    // 归还池（P2-1）
                _buffer = null;
            }
        }
    }
}

/// <summary>
/// 实体创建命令（链式：Add 组件/标签，延迟执行）。
/// </summary>
public readonly struct EntityCommand
{
    private readonly WorldCommandBuffer _owner;
    private readonly int _entityId;

    internal EntityCommand(WorldCommandBuffer owner, int entityId)
    {
        _owner = owner;
        _entityId = entityId;
    }

    /// <summary>链式添加组件。</summary>
    public EntityCommand Add<T>(in T component) where T : struct, IComponent
    {
        _owner.AddComponent(_entityId, component);
        return this;
    }

    /// <summary>链式添加标签。</summary>
    public EntityCommand AddTag<T>() where T : struct, ITag
    {
        _owner.AddTag<T>(_entityId);
        return this;
    }

    /// <summary>返回预留的实体 Id（Playback 后可用）。</summary>
    public int Id => _entityId;
}
