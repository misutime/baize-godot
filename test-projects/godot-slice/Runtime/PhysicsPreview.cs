// SPDX-License-Identifier: MIT
// PhysicsPreview.cs —— 物理域第一刀最小演示（O8；headless e2e 可跑）
//
// 目标：证明 GameWorld 碰撞/刚体语义 → PhysicsProjector → GodotPhysicsGateway → Jolt 链路通电：
// 一个静态地面 + 一个动态 cube（受重力下落），gateway 每帧采样权威位姿经 ObservationBus 回传。
// 物理步进由 SceneTree 每 physics tick 自动执行（active space）。
// 退出：渲染 360 帧（约 6 秒）后打印回传对比并 Quit（AGENTS §9 三十秒规则内）。

using Godot;
using Sola3d.Editor;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>最小物理切片演示：静态地面 + 动态 box 下落 + 位姿回传。</summary>
public sealed partial class PhysicsPreview : Node3D
{
	private GameWorld? _world;
	private PhysicsProjector? _projector;
	private GodotPhysicsGateway? _gateway;
	private ObservationBus? _observations;
	private int _frameCount;
	private int _observationCount;
	private float _initialY;
	private float _lastY;

	public override void _Ready()
	{
		_world = new GameWorld();
		_world.Schemas.Register<TransformComponent>();
		_world.Schemas.Register<StaticColliderComponent>();
		_world.Schemas.Register<RigidBodyComponent>();

		// 静态地面（y=0，大 box）。
		var ground = _world.CreateGameObject("Ground");
		ground.AddComponent<TransformComponent>().Position = new System.Numerics.Vector3(0, 0, 0);
		ground.AddComponent<StaticColliderComponent>().BoxSize = new System.Numerics.Vector3(20, 1, 20);

		// 动态 cube（y=5，受重力下落）。
		var cube = _world.CreateGameObject("Cube");
		cube.AddComponent<TransformComponent>().Position = new System.Numerics.Vector3(0, 5, 0);
		var rb = cube.AddComponent<RigidBodyComponent>();
		rb.BoxSize = new System.Numerics.Vector3(1, 1, 1);
		rb.Mass = 1f;
		_initialY = 5f;

		_projector = new PhysicsProjector();
		_observations = new ObservationBus();
		_observations.Subscribe(o =>
		{
			if (o is PhysicsObservation po)
			{
				_observationCount++;
				_lastY = po.Position.Y;
			}
		});
		_gateway = new GodotPhysicsGateway(_observations);
		_gateway.Initialize();
		GD.Print($"physics: 演示启动（地面 + 动态box@{_initialY}） gateway={_gateway.DebugInfo}");
	}

	public override void _Process(double delta)
	{
		_frameCount++;
		if (_world != null && _projector != null && _gateway != null)
		{
			// 每帧整帧投影 → gateway 内部做存活差异（同渲染 tracker 模式）。
			_gateway.Consume(_projector.Project(_world));
			// 演示未挂 Sola3dMainLoop：手动驱动帧末采样 + 观察分发（权威位姿回传）。
			_gateway.EndFrame((float)delta);
			_observations?.Dispatch();
		}
		if (_frameCount == 360)
		{
			bool fell = _lastY < _initialY - 0.5f;
			GD.Print($"physics: observations={_observationCount} initialY={_initialY:0.###} lastY={_lastY:0.###}（{(fell ? "下落 ✓" : "未下落 ✗")}） gateway={_gateway?.DebugInfo}");
			GetTree().Quit();
		}
	}

	public override void _ExitTree()
	{
		_gateway?.Dispose();
	}
}