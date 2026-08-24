// SPDX-License-Identifier: MIT
// PhysicsProjector.cs —— 物理域投影（O8 物理域第一刀）
//
// 模式同 SceneProjector（渲染域）：GameWorld 纯 C# 语义 → 物理命令流 → PhysicsGateway。
// 单向：只读投影，不反写 Gameplay；权威位姿由 PhysicsGateway 经 ObservationBus 回传。

using System.Collections.Generic;
using System.Numerics;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.Editor;

/// <summary>物理体模式：静态（StaticColliderComponent）/ 动态刚体（RigidBodyComponent）。</summary>
public enum PhysicsBodyKind
{
	/// <summary>静态碰撞体（仅碰撞，不受力）。</summary>
	Static,

	/// <summary>动态刚体（受力，Jolt 求解；每 fixed tick 回传位姿）。</summary>
	Rigid,
}

/// <summary>物理体注册命令（Gameplay → PhysicsGateway 下行负载）。</summary>
public sealed record PhysicsBodyCommand : GatewayCommand
{
	/// <summary>运行时对象身份（Index+Generation；物理权威位姿按此回传）。</summary>
	public ObjectId ObjectId { get; init; }

	/// <summary>物理体模式。</summary>
	public PhysicsBodyKind Kind { get; init; }

	/// <summary>Box 碰撞盒尺寸（X/Y/Z 全长）。</summary>
	public Vector3 BoxSize { get; init; } = Vector3.One;

	/// <summary>初始位置（世界）。</summary>
	public Vector3 Position { get; init; }

	/// <summary>初始旋转（世界）。</summary>
	public Quaternion Rotation { get; init; } = Quaternion.Identity;

	/// <summary>质量（kg；仅 Rigid 有效）。</summary>
	public float Mass { get; init; } = 1f;

	/// <summary>初始线速度（m/s；仅 Rigid 有效）。</summary>
	public Vector3 LinearVelocity { get; init; }
}

/// <summary>物理权威位姿观察（PhysicsGateway → Gameplay 上行负载：BodyGetState 采样）。</summary>
public sealed record PhysicsObservation : GatewayObservation
{
	/// <summary>对象稳定身份（Uid）。</summary>
	/// <summary>运行时对象身份（Index+Generation）。</summary>
	public ObjectId ObjectId { get; init; }

	/// <summary>权威位置（PhysicsServer/Jolt 求解后）。</summary>
	public Vector3 Position { get; init; }

	/// <summary>权威旋转。</summary>
	public Quaternion Rotation { get; init; }
}

/// <summary>
/// 把 GameWorld 的碰撞/刚体语义投影为物理体注册命令（纯 .NET，零 Godot 依赖）。
/// 遍历整棵对象树（DFS）：<see cref="StaticColliderComponent"/> → Static；<see cref="RigidBodyComponent"/> → Rigid。
/// </summary>
public sealed class PhysicsProjector
{
	/// <summary>投影整棵对象树，产出物理体命令列表。</summary>
	public List<PhysicsBodyCommand> Project(GameWorld world)
	{
		var commands = new List<PhysicsBodyCommand>();
		foreach (var root in world.Roots)
		{
			Walk(root, commands);
		}
		return commands;
	}

	private void Walk(Sola3d.GameObject.GameObject obj, List<PhysicsBodyCommand> commands)
	{
		var tf = obj.GetComponent<TransformComponent>();
		var staticCollider = obj.GetComponent<StaticColliderComponent>();
		var rigidBody = obj.GetComponent<RigidBodyComponent>();
		if (tf != null && staticCollider != null)
		{
			commands.Add(new PhysicsBodyCommand
			{
				ObjectId = obj.Id,
				Kind = PhysicsBodyKind.Static,
				BoxSize = staticCollider.BoxSize,
				Position = tf.Position,
				Rotation = tf.Rotation,
			});
		}
		else if (tf != null && rigidBody != null)
		{
			commands.Add(new PhysicsBodyCommand
			{
				ObjectId = obj.Id,
				Kind = PhysicsBodyKind.Rigid,
				BoxSize = rigidBody.BoxSize,
				Position = tf.Position,
				Rotation = tf.Rotation,
				Mass = rigidBody.Mass,
				LinearVelocity = rigidBody.LinearVelocity,
			});
		}
		foreach (var child in obj.Children)
		{
			Walk(child, commands);
		}
	}
}