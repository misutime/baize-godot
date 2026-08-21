// SPDX-License-Identifier: MIT
// WorldEvents.cs —— baize-godot EcsWorld 事件（P2.1，借鉴 Bevy EventWriter/EventReader）
//
// 系统间通信用纯数据事件（替代 Action 回调——S1-2 反思）：
// 写事件（EventWriter）与读事件（EventReader）显式分离，
// 系统声明"我发什么"或"我收什么"，不依赖系统引用顺序。

using System;
using System.Collections.Generic;

namespace Baize.Ecs;

/// <summary>
/// 世界事件总线：EventWriter 写入，EventReader 读取，Tick 切换时 Flush。
/// </summary>
public sealed class WorldEvents
{
    private readonly Dictionary<Type, List<object>> _current = new();
    private readonly Dictionary<Type, List<object>> _pending = new();

    /// <summary>获取写端（系统声明"我发事件"）。</summary>
    public EventWriter<T> Writer<T>() where T : struct => new(this);

    /// <summary>获取读端（系统声明"我收事件"）。</summary>
    public EventReader<T> Reader<T>() where T : struct => new(this);

    internal void Emit<T>(in T evt) where T : struct
    {
        if (!_pending.TryGetValue(typeof(T), out var list))
        {
            list = new List<object>();
            _pending[typeof(T)] = list;
        }
        list.Add(evt);
    }

    internal IReadOnlyList<object> ReadAll<T>() where T : struct
    {
        return _current.TryGetValue(typeof(T), out var list) ? list : Array.Empty<object>();
    }

    internal int ConsumeAll<T>() where T : struct
    {
        if (!_current.TryGetValue(typeof(T), out var list)) return 0;
        int count = list.Count;
        _current.Remove(typeof(T));
        return count;
    }

    /// <summary>Tick 切换：pending → current（EcsWorld 调用）。</summary>
    public void Flush()
    {
        _current.Clear();
        foreach (var kv in _pending)
        {
            _current[kv.Key] = kv.Value;
        }
        _pending.Clear();
    }

    /// <summary>清空所有事件（Reset 用）。</summary>
    public void Reset()
    {
        _current.Clear();
        _pending.Clear();
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

/// <summary>事件读端（系统用它收事件）。</summary>
public readonly struct EventReader<T> where T : struct
{
    private readonly WorldEvents _events;

    internal EventReader(WorldEvents events) => _events = events;

    /// <summary>读取本 Tick 所有事件（不消费）。</summary>
    public IReadOnlyList<object> Read() => _events.ReadAll<T>();

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
