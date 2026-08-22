// SPDX-License-Identifier: MIT
// EcsFeatureAttributes.cs —— Feature 注册源生成器的元数据特性
//
// 用 attribute 声明"这个 Feature 启用哪些 System（在哪个 Phase）、组合哪些子 Feature"，
// 源生成器据此产出 partial Feature 的 Install 方法——声明即注册，无反射。
// 生成代码是普通 C#（可单步、可读），不隐藏 Feature 边界/Phase/因果顺序。

using System;

namespace Baize.Ecs;

/// <summary>标记一个 partial Feature 类：其 Install 方法由源生成器产出。</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class EcsFeatureAttribute : Attribute { }

/// <summary>声明本 Feature 在指定 Phase 注册某 System（生成器产出 AddSystem 调用）。</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class AddSystemAttribute<TSystem> : Attribute
{
    public Phase Phase { get; }

    public AddSystemAttribute(Phase phase) => Phase = phase;
}

/// <summary>声明本 Feature 组合某个子 Feature（生成器产出 AddFeature 调用）。</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class AddFeatureAttribute<TFeature> : Attribute { }
