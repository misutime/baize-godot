// SPDX-License-Identifier: MIT
// WorldCommandBuffer.cs —— baize-godot EcsWorld 命令缓冲（P2.1）
//
// 延迟结构变更：系统查询循环内禁止直接 CreateEntity/DeleteEntity/AddComponent
// （Friflo 抛 StructuralChangeException），统一走 CommandBuffer，在 Tick 末尾 Playback。

using System.Collections.Generic;
using Friflo.Engine.ECS;

namespace Baize.Ecs;

/// <summary>
/// 世界命令缓冲：延迟执行结构变更（创建/删除实体、添加组件）。
/// </summary>
public sealed class WorldCommandBuffer
{
    private readonly EntityStore _store;
    private readonly Friflo.Engine.ECS.CommandBuffer _buffer;
    private readonly List<Entity> _created = new();

    internal WorldCommandBuffer(EntityStore store)
    {
        _store = store;
        _buffer = store.GetCommandBuffer();
    }

    /// <summary>创建实体（延迟到 Playback 实际创建）。</summary>
    public EntityCommand CreateEntity()
    {
        var command = new EntityCommand(this);
        _buffer.CreateEntity();
        return command;
    }

    /// <summary>删除实体（延迟）。</summary>
    public void DeleteEntity(int entityId)
    {
        _buffer.DeleteEntity(entityId);
    }

    /// <summary>给实体添加组件（延迟）。</summary>
    public void AddComponent<T>(int entityId, in T component) where T : struct, IComponent
    {
        _buffer.AddComponent(entityId, component);
    }

    /// <summary>播放所有命令（Tick 末尾调用，系统内勿调）。</summary>
    public void Playback()
    {
        _buffer.Playback();
    }

    /// <summary>清空缓冲（Reset 用）。</summary>
    public void Reset()
    {
        _buffer.Clear();
        _created.Clear();
    }
}

/// <summary>实体创建命令（链式添加组件，延迟执行）。</summary>
public readonly struct EntityCommand
{
    private readonly WorldCommandBuffer _owner;

    internal EntityCommand(WorldCommandBuffer owner)
    {
        _owner = owner;
    }

    // 延迟创建：Friflo CommandBuffer.CreateEntity 返回 int id（Playback 后才有实体）
    // 组件添加在 Playback 前通过 commandBuffer 记录，本结构体只是链式语法载体。
}
