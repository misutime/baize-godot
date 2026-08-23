// SPDX-License-Identifier: MIT
// StaticColliderComponent.cs —— 静态碰撞体数据组件（O6，O6-GameWorldBackend与垂直切片.md §2）
//
// 纯数据：BoxSize 描述静态碰撞盒（O6 最小切片只做 Box）。
// 真实 PhysicsServer 注册由 PhysicsBackend 完成（状态 C——不挂 StaticBody3D 节点）。

using System.Numerics;

namespace Sola3d.GameObject;

/// <summary>静态碰撞盒（Box 形状；PhysicsBackend 投影到 PhysicsServer/Jolt）。</summary>
[GameComponent]
public sealed class StaticColliderComponent : GameComponent
{
	[GameProperty]
	public Vector3 BoxSize { get; set; } = Vector3.One;
}
