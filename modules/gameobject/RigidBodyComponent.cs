// SPDX-License-Identifier: MIT
// RigidBodyComponent.cs —— 动态刚体数据组件（O8 物理域第一刀）
//
// 纯数据：Box 形状 + 质量 + 初速度；不挂 RigidBody3D 节点。
// 真实 PhysicsServer/Jolt 注册由 PhysicsGateway 完成（状态 C——Server-backed）。

using System.Numerics;

namespace Sola3d.GameObject;

/// <summary>动态刚体（Box 形状；PhysicsGateway 投影到 PhysicsServer/Jolt，固定步长回传权威位姿）。</summary>
[GameComponent]
public sealed class RigidBodyComponent : GameComponent
{
	/// <summary>碰撞盒尺寸（局部轴，三半程量未做，直接全长语义）。</summary>
	[GameProperty]
	public Vector3 BoxSize { get; set; } = Vector3.One;

	/// <summary>质量（kg）。</summary>
	[GameProperty]
	public float Mass { get; set; } = 1f;

	/// <summary>初始线速度（m/s）。</summary>
	[GameProperty]
	public Vector3 LinearVelocity { get; set; } = Vector3.Zero;
}