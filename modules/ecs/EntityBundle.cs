// SPDX-License-Identifier: MIT
// EntityBundle.cs —— baize-godot EcsWorld 组件组合（P2.1，借鉴 Bevy Bundle）
//
// 组件组合包：一次创建实体时添加一组组件（对应 W1 Object = 组件组合 心智模型）。
// 装配时用 world.SpawnNow(bundle)，系统查询期间用 world.CommandBuffer.Spawn(bundle)。

using System;
using Friflo.Engine.ECS;

namespace Baize.Ecs;

/// <summary>
/// 组件组合接口：实现类声明一组组件，可一次创建实体。
/// 用法：struct PlayerBundle : IEntityBundle { ... } → world.SpawnNow(new PlayerBundle{...})
/// </summary>
public interface IEntityBundle
{
    /// <summary>把本组合的组件应用到实体创建命令。</summary>
    void Apply(in EntityCommand entity);
}

/// <summary>
/// 组件组合工具：把 IEntityBundle 一次创建为实体（延迟，走 CommandBuffer）。
/// </summary>
public static class EntityBundleExtensions
{
    /// <summary>用组合包创建实体（延迟到 Playback）。</summary>
    public static EntityCommand Spawn(this WorldCommandBuffer buffer, IEntityBundle bundle)
    {
        var entity = buffer.CreateEntity();
        bundle.Apply(entity);
        return entity;
    }

    /// <summary>给已创建实体应用组合包（添加组件）。</summary>
    public static void ApplyTo(this WorldCommandBuffer buffer, int entityId, IEntityBundle bundle)
    {
        // 通过 EntityCommand 应用（复用链式 Add）
        bundle.Apply(new EntityCommand(buffer, entityId));
    }
}
