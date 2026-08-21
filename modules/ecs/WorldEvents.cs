// SPDX-License-Identifier: MIT
// WorldEvents.cs —— baize-godot EcsWorld 事件（P2.1，借鉴 Bevy EventWriter/EventReader）
//
// 系统间通信用纯数据事件（替代 Action 回调——S1-2 反思）：
// 写事件（EventWriter）与读事件（EventReader）显式分离。
// P2-3 修复：按事件类型存 List<T>（无装箱）+ 双缓冲复用（无每 Tick 分配）+ 类型化 Reader。

using System;
using System.Collections.Generic;

namespace Baize.Ecs;

/// <summary>
/// 世界事件总线：EventWriter 写入，EventReader 读取，Tick 切换时 Flush。
/// </summary>
public sealed class WorldEvents
{
    // 按事件类型存储的 List<T>（双缓冲：pending 写入 / current 读取）
    private sealed class EventBuffer<T> where T : struct
    {
        public readonly List<T> Pending = new();
        public readonly List<T> Current = new();
    }

    // 用泛型 static 缓存每类型的缓冲（无装箱，无每 Tick 分配）
    private static class BufferCache<T> where T : struct
    {
        public static EventBuffer<T> Instance { get; } = new();
    }

    /// <summary>获取写端（系统声明"我发事件"）。</summary>
    public EventWriter<T> Writer<T>() where T : struct => new(this);

    /// <summary>获取读端（系统声明"我收事件"）。</summary>
    public EventReader<T> Reader<T>() where T : struct => new(this);

    internal void Emit<T>(in T evt) where T : struct
    {
        EnsureFlusher<T>();                        // 首次发送注册 Flusher
        BufferCache<T>.Instance.Pending.Add(evt);  // List<T>，无装箱
    }

    internal IReadOnlyList<T> ReadAll<T>() where T : struct
    {
        return BufferCache<T>.Instance.Current;
    }

    internal int ConsumeAll<T>() where T : struct
    {
        var buf = BufferCache<T>.Instance;
        int count = buf.Current.Count;
        buf.Current.Clear();
        return count;
    }

    /// <summary>Tick 切换：pending → current（EcsWorld 调用，双缓冲复用）。</summary>
    public void Flush()
    {
        // 遍历所有已用类型（缓存注册）
        foreach (var pair in _usedTypes)
        {
            var method = pair.FlushAction;
            method();
        }
    }

    private readonly List<(Type Type, Action FlushAction)> _usedTypes = new();

    private void EnsureFlusher<T>() where T : struct
    {
        // 按类型检查是否已注册（不能只用一个 bool——每个类型都要注册 Flusher）
        var t = typeof(T);
        foreach (var pair in _usedTypes)
        {
            if (pair.Type == t) return;
        }
        _usedTypes.Add((t, () =>
        {
            var buf = BufferCache<T>.Instance;
            buf.Current.Clear();
            foreach (var e in buf.Pending) buf.Current.Add(e);
            buf.Pending.Clear();
        }));
    }

    /// <summary>清空所有事件（Reset 用）。</summary>
    public void Reset()
    {
        foreach (var pair in _usedTypes)
        {
            pair.FlushAction();
            // 清空 current
        }
        _usedTypes.Clear();
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

