// SPDX-License-Identifier: MIT
// WorldState.cs —— baize-godot EcsWorld 世界状态单例（P2.1，借鉴 Bevy Resource）
//
// 全局单例数据（GameState/Score/配置）——不挂实体，EcsWorld 持有。
// 解决"全局状态设计成组件挂实体"的不自然（Bevy Resource 概念）。

using System;
using System.Collections.Generic;

namespace Baize.Ecs;

public sealed class WorldState
{
    private readonly Dictionary<Type, object> _resources = new();

    /// <summary>设置/覆盖一个全局状态。</summary>
    public void Set<T>(T value) where T : class
    {
        _resources[typeof(T)] = value;
    }

    /// <summary>获取全局状态（不存在返回 null）。</summary>
    public T? Get<T>() where T : class
    {
        return _resources.TryGetValue(typeof(T), out var v) ? (T)v : null;
    }

    /// <summary>获取全局状态（不存在抛异常）。</summary>
    public T GetOrThrow<T>() where T : class
    {
        if (_resources.TryGetValue(typeof(T), out var v)) return (T)v;
        throw new InvalidOperationException($"State not found: {typeof(T).Name}");
    }

    /// <summary>移除全局状态。</summary>
    public bool Remove<T>() where T : class
    {
        return _resources.Remove(typeof(T));
    }

    /// <summary>是否包含某状态。</summary>
    public bool Has<T>() where T : class => _resources.ContainsKey(typeof(T));

    /// <summary>清空所有状态（Reset 用）。</summary>
    public void Clear() => _resources.Clear();
}
