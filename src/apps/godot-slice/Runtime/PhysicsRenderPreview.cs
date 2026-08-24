// SPDX-License-Identifier: MIT
// PhysicsRenderPreview.cs —— P0 物理→渲染联动演示（O8；真窗口）
//
// 目标：两个 Gateway 首次协作——物理权威位姿（GodotPhysicsGateway → ObservationBus）
// 写回 GameWorld Transform，再经 SceneProjector → RenderSnapshotTracker → GodotRenderGateway
// 投影：屏幕上可见物理物体受重力下落（render 跟随 physics，不再是两个孤立切片）。
// 地面=静态体（带 Mesh 可视），cube=动态刚体（带 Mesh，y=5 下落）。
// 退出：360 帧（约 6 秒）打印回传对比 + 截图 user://physics_render.png 并 Quit。

using Godot;
using Sola3d.Editor;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>物理→渲染联动最小闭环：physics 权威位姿回传写回 GameWorld → render 投影跟随。</summary>
public sealed partial class PhysicsRenderPreview : Node3D
{
	private GameWorld? _world;
	private PhysicsProjector? _physicsProjector;
	private GodotPhysicsGateway? _physicsGateway;
	private ObservationBus? _observations;
	private SceneProjector? _sceneProjector;
	private RenderSnapshotTracker? _tracker;
	private GodotRenderGateway? _renderGateway;
	private int _frameCount;
	private float _initialY = 5f;
	private float _lastY = 5f;

	public override void _Ready()
	{
		_world = new GameWorld();
		_world.Schemas.Register<TransformComponent>();
		_world.Schemas.Register<MeshComponent>();
		_world.Schemas.Register<StaticColliderComponent>();
		_world.Schemas.Register<RigidBodyComponent>();

		// 地面：静态体 + 可视 Mesh。
		var ground = _world.CreateGameObject("Ground");
		ground.AddComponent<TransformComponent>();
		ground.AddComponent<MeshComponent>().MeshPath = "res://primitive/ground.mesh";
		ground.AddComponent<StaticColliderComponent>().BoxSize = new System.Numerics.Vector3(20, 1, 20);

		// cube：动态刚体 + 可视 Mesh，y=5 受重力下落。
		var cube = _world.CreateGameObject("Cube");
		cube.AddComponent<TransformComponent>().Position = new System.Numerics.Vector3(0, 5, 0);
		cube.AddComponent<MeshComponent>().MeshPath = "res://primitive/cube.mesh";
		var rb = cube.AddComponent<RigidBodyComponent>();
		rb.BoxSize = new System.Numerics.Vector3(1, 1, 1);
		rb.Mass = 1f;

		// 物理侧：投影 → gateway → ObservationBus → 写回 world。
		_physicsProjector = new PhysicsProjector();
		_observations = new ObservationBus();
		_observations.Subscribe(WriteBack);
		_physicsGateway = new GodotPhysicsGateway(_observations);
		_physicsGateway.Initialize();

		// 渲染侧：同 DemoPreview 模式——SceneProjector + tracker + GodotRenderGateway。
		_sceneProjector = new SceneProjector();
		_tracker = new RenderSnapshotTracker();
		_renderGateway = new GodotRenderGateway();
		_renderGateway.Initialize(GetViewport().GetWorld3D().Scenario);
		_renderGateway.Consume(_tracker.Diff(_sceneProjector.Project(_world)));

		// 相机：Camera3D 只是视图投射器（Godot 内部处理 Viewport attach）。
		var cam = new Camera3D { Fov = 60.0f, Near = 0.05f, Far = 100.0f };
		cam.Position = new Vector3(3.5f, 3.0f, 5.0f);
		AddChild(cam);
		cam.LookAt(new Vector3(0, 1, 0), Vector3.Up);
		cam.MakeCurrent();

		GD.Print($"physics-render: 联动演示启动（cube@{_initialY}） physics={_physicsGateway.DebugInfo} render={_renderGateway.DebugInfo}");
	}

	/// <summary>物理权威位姿写回 GameWorld Transform（Gameplay 消费 ObservationBus 的示范路径）。</summary>
	private void WriteBack(GatewayObservation observation)
	{
		if (observation is not PhysicsObservation po || _world == null)
		{
			return;
		}
		var obj = _world.GetObject(po.ObjectId);
		var tf = obj?.GetComponent<TransformComponent>();
		if (tf != null)
		{
			tf.Position = po.Position;
			tf.Rotation = po.Rotation;
			_lastY = po.Position.Y;
		}
	}

	public override void _Process(double delta)
	{
		_frameCount++;
		if (_world == null || _physicsProjector == null || _physicsGateway == null || _observations == null
			|| _sceneProjector == null || _tracker == null || _renderGateway == null)
		{
			return;
		}

		// 1) 物理：投影 → consume → 帧末采样 → 分发（触发 WriteBack 写回 GameWorld）。
		_physicsGateway.Consume(_physicsProjector.Project(_world));
		_physicsGateway.EndFrame((float)delta);
		_observations.Dispatch();

		// 2) 渲染：重新投影（读到的是物理写回后的最新 Transform）→ 帧差异 → 投影到 RenderingServer。
		_renderGateway.Consume(_tracker.Diff(_sceneProjector.Project(_world)));

		if (_frameCount == 360)
		{
			bool fell = _lastY < _initialY - 0.5f;
			GD.Print($"physics-render: lastY={_lastY:0.###}（{(fell ? "下落 ✓（render 已跟随 physics）" : "未下落 ✗")}） physics={_physicsGateway.DebugInfo} render={_renderGateway.DebugInfo}");
			var img = GetViewport().GetTexture().GetImage();
			string path = ProjectSettings.GlobalizePath("user://physics_render.png");
			img.SavePng(path);
			GD.Print($"physics-render: screenshot saved → user://physics_render.png");
			GetTree().Quit();
		}
	}

	public override void _ExitTree()
	{
		_physicsGateway?.Dispose();
		_renderGateway?.Dispose();
	}
}