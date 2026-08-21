// SPDX-License-Identifier: MIT
// WorldEvents.cs —— baize-godot EcsWorld 事件（P2.1）
//
// 系统间通信用纯数据事件（替代 Action 回调——S1-2 反思）：
// 系统 A 发事件，系统 B 消费。事件是纯数据（可序列化/回放），
// 不依赖系统引用顺序。

using System.Collections.Generic;

namespace Baize.Ecs;

/// <summary>
/// 世界事件总线：Tick 内系统可发事件，下阶段系统消费。
/// </summary>
public sealed class WorldEvents
{
    private readonly Dictionary<System.Type, List<object>> _events = new();
    private readonly Dictionary<System.Type, List<object>> _pending = new();

    /// <summary>发出一个事件（本 Tick 可被后续系统读取）。</summary>
    public void Emit<T>(in T evt) where T : struct
    {
        if (!_pending.TryGetValue(typeof(T), out var list))
        {
            list = new List<object>();
            _pending[typeof(T)] = list;
        }
        list.Add(evt);
    }

    /// <summary>读取本 Tick 所有事件（消费后保留，供查询）。</summary>
    public IReadOnlyList<object> Get<T>() where T : struct
    {
        return _events.TryGetValue(typeof(T), out var list) ? list : System.Array.Empty<object>();
    }

    /// <summary>读取并消费本 Tick 所有事件（处理后清空）。</summary>
    public int Consume<T>() where T : struct
    {
        if (!_events.TryGetValue(typeof(T), out var list)) return 0;
        int count = list.Count;
        _events.Remove(typeof(T));
        return count;
    }

    /// <summary>Tick 切换：pending → events（EcsWorld 调用）。</summary>
    public void Flush()
    {
        _events.Clear();
        foreach (var kv in _pending)
        {
            _events[kv.Key] = kv.Value;
        }
        _pending.Clear();
    }

    /// <summary>清空所有事件（Reset 用）。</summary>
    public void Reset()
    {
        _events.Clear();
        _pending.Clear();
    }
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
