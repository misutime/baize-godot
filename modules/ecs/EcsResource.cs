// SPDX-License-Identifier: MIT
// EcsResource.cs —— baize-godot EcsWorld 全局单例（P2.1，借鉴 Bevy Resource）
//
// 全局单例数据（GameState/Score/配置）——不挂实体，EcsWorld 持有。
// 解决"全局状态设计成组件挂实体"的不自然（Bevy Resource 概念）。

using System;
using System.Collections.Generic;

namespace Baize.Ecs;

/// <summary>
/// 全局单例资源存储（借鉴 Bevy Resource）。
/// 用法：world.SetResource(new Score()); 系统读 world.GetResource&lt;Score&gt;()。
/// </summary>
public sealed class EcsResource
{
    private readonly Dictionary<Type, object> _resources = new();

    /// <summary>设置/覆盖一个全局资源。</summary>
    public void Set<T>(T value) where T : class
    {
        _resources[typeof(T)] = value;
    }

    /// <summary>获取全局资源（不存在返回 null）。</summary>
    public T? Get<T>() where T : class
    {
        return _resources.TryGetValue(typeof(T), out var v) ? (T)v : null;
    }

    /// <summary>获取全局资源（不存在抛异常）。</summary>
    public T GetOrThrow<T>() where T : class
    {
        if (_resources.TryGetValue(typeof(T), out var v)) return (T)v;
        throw new InvalidOperationException($"Resource not found: {typeof(T).Name}");
    }

    /// <summary>移除全局资源。</summary>
    public bool Remove<T>() where T : class
    {
        return _resources.Remove(typeof(T));
    }

    /// <summary>是否包含某资源。</summary>
    public bool Has<T>() where T : class => _resources.ContainsKey(typeof(T));

    /// <summary>清空所有资源（Reset 用）。</summary>
    public void Clear() => _resources.Clear();
}
