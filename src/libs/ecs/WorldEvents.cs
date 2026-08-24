// SPDX-License-Identifier: MIT
// WorldEvents.cs —— sola3d-godot EcsWorld 事件（P2.1，借鉴 Bevy EventWriter/EventReader）
//
// 系统间通信用纯数据事件（替代 Action 回调——S1-2 反思）：
// 写事件（EventWriter）与读事件（EventReader）显式分离。
// P2-3 修复：按事件类型存 List<T>（无装箱）+ 双缓冲复用（无每 Tick 分配）+ 类型化 Reader。
// review 第2轮修复：事件缓冲为 WorldEvents 实例所有（非静态共享——多世界隔离）；
// Reset 清空 pending + current（不复用 FlushAction 提升 pending）。

using System;
using System.Collections.Generic;

namespace Sola3d.Ecs;

/// <summary>
/// 世界事件总线：EventWriter 写入，EventReader 读取，Tick 切换时 Flush。
/// 每实例独立缓冲（多 EcsWorld 互不污染）。
/// </summary>
public sealed class WorldEvents
{
    // 类型化事件 holder（实例级——P1-1 修复：非静态共享）
    private interface IEventHolder
    {
        void Flush();
        void ClearAll();
    }

    private sealed class EventHolder<T> : IEventHolder where T : struct
    {
        public readonly List<T> Pending = new();
        public readonly List<T> Current = new();

        public void Flush()
        {
            Current.Clear();
            foreach (var e in Pending) Current.Add(e);
            Pending.Clear();
        }

        public void ClearAll()
        {
            Pending.Clear();
            Current.Clear();
        }
    }

    private readonly Dictionary<Type, IEventHolder> _holders = new();

    private EventHolder<T> GetHolder<T>() where T : struct
    {
        if (!_holders.TryGetValue(typeof(T), out var holder))
        {
            holder = new EventHolder<T>();
            _holders[typeof(T)] = holder;
        }
        return (EventHolder<T>)holder;
    }

    /// <summary>获取写端（系统声明"我发事件"）。</summary>
    public EventWriter<T> Writer<T>() where T : struct => new(this);

    /// <summary>获取读端（系统声明"我收事件"）。</summary>
    public EventReader<T> Reader<T>() where T : struct => new(this);

    internal void Emit<T>(in T evt) where T : struct
    {
        GetHolder<T>().Pending.Add(evt);   // List<T>，无装箱
    }

    internal IReadOnlyList<T> ReadAll<T>() where T : struct
    {
        return GetHolder<T>().Current;
    }

    internal int ConsumeAll<T>() where T : struct
    {
        var holder = GetHolder<T>();
        int count = holder.Current.Count;
        holder.Current.Clear();
        return count;
    }

    /// <summary>Tick 切换：pending → current（EcsWorld 调用，双缓冲复用）。</summary>
    public void Flush()
    {
        foreach (var holder in _holders.Values)
        {
            holder.Flush();
        }
    }

    /// <summary>清空所有事件（Reset 用，P1-2 修复：pending + current 都清，不提升 pending）。</summary>
    public void Reset()
    {
        foreach (var holder in _holders.Values)
        {
            holder.ClearAll();
        }
    }
}

/// <summary>事件写端（系统用它发事件）。</summary>
public readonly struct EventWriter<T> where T : struct
{
    private readonly WorldEvents _events;

    internal EventWriter(WorldEvents events) => _events = events;

    /// <summary>发送一个事件。</summary>
    public void Send(in T evt) => _events.Emit(evt);
}

/// <summary>事件读端（系统用它收事件，类型化视图——P2-3 修复无装箱）。</summary>
public readonly struct EventReader<T> where T : struct
{
    private readonly WorldEvents _events;

    internal EventReader(WorldEvents events) => _events = events;

    /// <summary>读取本 Tick 所有事件（类型化，不消费）。</summary>
    public IReadOnlyList<T> Read() => _events.ReadAll<T>();

    /// <summary>读取并消费本 Tick 所有事件（返回数量）。</summary>
    public int Consume() => _events.ConsumeAll<T>();
}

// 通用事件类型（示例游戏会用到的，供参考）
public readonly struct DamageRequest
{
    public readonly int TargetId;
    public readonly int Amount;

    public DamageRequest(int targetId, int amount)
    {
        TargetId = targetId;
        Amount = amount;
    }
}

public readonly struct DeathEvent
{
    public readonly int EntityId;

    public DeathEvent(int entityId)
    {
        EntityId = entityId;
    }
}
