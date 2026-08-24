// SPDX-License-Identifier: MIT
// GodotPhysicsGateway.cs —— PhysicsServer3D/Jolt 真桥（O8 物理域第一刀）
//
// 状态 C（Server-backed）物理桥：不挂 StaticBody3D/RigidBody3D 节点——直接
// GameWorld 语义 → PhysicsBodyCommand → 本 Gateway → PhysicsServer3D RID。
// 位姿权威在 PhysicsServer/Jolt：每帧 BodyGetState 采样 → ObservationBus 回传（Gateway → Gameplay）。
// 物理步进由 SceneTree 每个 physics tick 自动执行当前全局所有 active space（PhysicsServer3D.step 无公开 C# 绑定）。

using Godot;
using Sola3d.Editor;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>PhysicsServer3D 真桥：space/body/shape RID 生命周期 + 位姿回传。</summary>
public sealed partial class GodotPhysicsGateway : IPhysicsGateway
{
	private sealed class RegisteredBody
	{
		public required Rid Body { get; init; }
		public required PhysicsBodyKind Kind { get; init; }
	}

	private readonly ObservationBus _observations;
	private readonly System.Collections.Generic.Dictionary<ObjectId, RegisteredBody> _bodies = new();
	private readonly System.Collections.Generic.Dictionary<ObjectId, Rid> _shapes = new();
	private readonly System.Collections.Generic.HashSet<ObjectId> _alive = new();
	private readonly System.Collections.Generic.List<Rid> _ownedRids = new();
	private Rid _space;
	private bool _disposed;
	private bool _sampleLogged;

	public GodotPhysicsGateway(ObservationBus observations)
	{
		_observations = observations ?? throw new System.ArgumentNullException(nameof(observations));
	}

	/// <summary>space 是否已创建（真实环境 Initialize 后为 true）。</summary>
	public bool IsInitialized => _space.IsValid;

	/// <summary>创建物理空间（active——SceneTree 每 physics tick 自动步进该 space）。</summary>
	public void Initialize()
	{
		_space = PhysicsServer3D.SpaceCreate();
		PhysicsServer3D.SpaceSetActive(_space, true);
	}

	public void BeginFrame(float nowSeconds)
	{
	}

	/// <summary>帧末：采样动态刚体权威位姿 → ObservationBus（Sola3dMainLoop 下一帧 Dispatch 分发）。</summary>
	public void EndFrame(float nowSeconds)
	{
		foreach (var pair in _bodies)
		{
			if (pair.Value.Kind != PhysicsBodyKind.Rigid)
			{
				continue;
			}
			var t = (Transform3D)PhysicsServer3D.BodyGetState(pair.Value.Body, PhysicsServer3D.BodyState.Transform);
			if (!_sampleLogged)
			{
				_sampleLogged = true;
				GD.Print($"physics: 采样 {pair.Key} y={t.Origin.Y:0.###}");
			}
			var q = t.Basis.GetRotationQuaternion();
			_observations.Submit(new PhysicsObservation
			{
				ObjectId = pair.Key,
				Position = new System.Numerics.Vector3(t.Origin.X, t.Origin.Y, t.Origin.Z),
				Rotation = new System.Numerics.Quaternion(q.X, q.Y, q.Z, q.W),
			});
		}
	}

	/// <summary>消费整帧物理命令（upsert）；与渲染 tracker 同理做存活差异：上帧有、本帧无 → 释放。</summary>
	public void Consume(System.Collections.Generic.IReadOnlyList<GatewayCommand> commands)
	{
		var frame = new System.Collections.Generic.HashSet<ObjectId>();
		foreach (var c in commands)
		{
			if (c is PhysicsBodyCommand bc)
			{
				Apply(bc);
				frame.Add(bc.ObjectId);
			}
		}
		foreach (var id in _alive)
		{
			if (!frame.Contains(id))
			{
				Remove(id);
			}
		}
		_alive.Clear();
		_alive.UnionWith(frame);
	}

	private void Apply(PhysicsBodyCommand bc)
	{
		if (_bodies.TryGetValue(bc.ObjectId, out var existing))
		{
			// 已注册：静态体位姿来自 GameWorld（每帧跟随）；动态刚体权威位姿在 PhysicsServer
			//（每帧投影不覆盖，避免把刚体钉回初始位置——否则重力积分被重置，永远不动）。
			if (existing.Kind == PhysicsBodyKind.Static)
			{
				PhysicsServer3D.BodySetState(existing.Body, PhysicsServer3D.BodyState.Transform, ToTransform(bc));
			}
			return;
		}

		var body = PhysicsServer3D.BodyCreate();
		PhysicsServer3D.BodySetSpace(body, _space);
		var shape = PhysicsServer3D.BoxShapeCreate();
		PhysicsServer3D.ShapeSetData(shape, GodotVector(bc.BoxSize));
		PhysicsServer3D.BodyAddShape(body, shape);
		_ownedRids.Add(shape);
		_shapes[bc.ObjectId] = shape;

		if (bc.Kind == PhysicsBodyKind.Rigid)
		{
			PhysicsServer3D.BodySetMode(body, PhysicsServer3D.BodyMode.Rigid);
			PhysicsServer3D.BodySetParam(body, PhysicsServer3D.BodyParameter.Mass, bc.Mass);
			PhysicsServer3D.BodySetState(body, PhysicsServer3D.BodyState.LinearVelocity, GodotVector(bc.LinearVelocity));
		}
		else
		{
			PhysicsServer3D.BodySetMode(body, PhysicsServer3D.BodyMode.Static);
		}
		PhysicsServer3D.BodySetState(body, PhysicsServer3D.BodyState.Transform, ToTransform(bc));
		_bodies[bc.ObjectId] = new RegisteredBody { Body = body, Kind = bc.Kind };
		GD.Print($"physics: body 注册 id={bc.ObjectId} kind={bc.Kind} pos={bc.Position}");
	}

	private void Remove(ObjectId objectId)
	{
		if (_bodies.Remove(objectId, out var entry))
		{
			PhysicsServer3D.FreeRid(entry.Body);
		}
		if (_shapes.Remove(objectId, out var shape))
		{
			PhysicsServer3D.FreeRid(shape);
		}
		GD.Print($"physics: body 释放 id={objectId}");
	}

	private static Transform3D ToTransform(PhysicsBodyCommand bc)
	{
		var q = new Quaternion(bc.Rotation.X, bc.Rotation.Y, bc.Rotation.Z, bc.Rotation.W);
		return new Transform3D(new Basis(q), GodotVector(bc.Position));
	}

	private static Vector3 GodotVector(System.Numerics.Vector3 v) => new(v.X, v.Y, v.Z);

	/// <summary>释放全部 RID（幂等）。</summary>
	public void Dispose()
	{
		if (_disposed)
		{
			return;
		}
		_disposed = true;
		foreach (var entry in _bodies.Values)
		{
			PhysicsServer3D.FreeRid(entry.Body);
		}
		foreach (var rid in _ownedRids)
		{
			PhysicsServer3D.FreeRid(rid);
		}
		if (_space.IsValid)
		{
			PhysicsServer3D.FreeRid(_space);
		}
		_bodies.Clear();
		_shapes.Clear();
		_alive.Clear();
		_ownedRids.Clear();
	}

	/// <summary>诊断：body/space 状态。</summary>
	public string DebugInfo => $"bodies={_bodies.Count} space={(_space.IsValid ? "valid" : "INVALID")} disposed={_disposed}";
}