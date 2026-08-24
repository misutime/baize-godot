// SPDX-License-Identifier: MIT
// ShooterGamePreview.cs —— O2 玩法对接（M1 缺口②）：零 Godot 玩法 + 3D 可视化
//
// 目标：把 O2 纯逻辑射击玩法（玩家/敌人/子弹/命中/计分/死亡，X/Z 平面）
// 接上已打通的渲染 + 输入链路，成为"可运行的射击小游戏"最小形态：
//   Godot 输入 → GodotInputGateway → InputState → O2 InputService
//   → ShooterGame.RunFrame（Move/Collide/Tick，纯 C# 玩法权威）
//   → RenderSync（Position→Transform+按阵营 Mesh）→ SceneProjector → tracker → GodotRenderGateway
// 玩法逻辑 100% 在 O2（零 Godot）；本层只做输入桥、渲染同步与视图。

using Godot;
using Shooter.Objects;
using Sola3d.Editor;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>玩法→渲染同步：把 O2 Position(X/Z) 同步到 TransformComponent，并按阵营挂 Mesh。</summary>
public sealed class RenderSyncComponent : GameComponent
{
	public const string MeshPathPlayer = "res://meshes/player.mesh";
	public const string MeshPathEnemy = "res://meshes/enemy.mesh";
	public const string MeshPathBullet = "res://meshes/bullet.mesh";
	public const string MeshPathOther = "res://meshes/other.mesh";

	public override void OnTick(float delta)
	{
		var pos = Owner!.GetComponent<Position>();
		if (pos == null)
		{
			return;
		}
		var tf = Owner.GetComponent<TransformComponent>();
		if (tf == null)
		{
			tf = Owner.AddComponent<TransformComponent>();
			tf.Position = new System.Numerics.Vector3(pos.X, 0, pos.Z);
		}
		else
		{
			tf.Position = new System.Numerics.Vector3(pos.X, 0, pos.Z);
		}
		if (Owner.GetComponent<MeshComponent>() == null)
		{
			Owner.AddComponent(new MeshComponent { MeshPath = ResolveMeshPath() });
		}
	}

	private string ResolveMeshPath()
	{
		if (Owner?.GetComponent<PlayerFaction>() != null)
		{
			return MeshPathPlayer;
		}
		if (Owner?.GetComponent<EnemyFaction>() != null)
		{
			return MeshPathEnemy;
		}
		if (Owner?.GetComponent<ProjectileTag>() != null)
		{
			return MeshPathBullet;
		}
		return MeshPathOther;
	}
}

/// <summary>玩法对接演示（--shooter）：O2 玩法 + 输入 + 渲染合一。</summary>
public sealed partial class ShooterGamePreview : Node3D
{
	private GameWorld? _world;
	private InputState? _inputState;
	private GodotInputGateway? _input;
	private SceneProjector? _sceneProjector;
	private RenderSnapshotTracker? _tracker;
	private GodotRenderGateway? _renderGateway;
	private int _frameCount;
	private int _lastScore = -1;
	private int _lastAlive = -1;
	private bool _overReported;
	private bool _headless;

	public override void _Ready()
	{
		_headless = DisplayServer.GetName() == "headless";
		_world = ShooterGame.CreateWorld();
		_world.Schemas.Register<TransformComponent>();
		_world.Schemas.Register<MeshComponent>();

		// 输入资源：gateway → InputState（玩法每帧桥到 O2 InputService）。
		_inputState = new InputState();
		_world.AddResource(_inputState);
		_input = new GodotInputGateway();

		// 渲染侧仅在真窗口初始化（headless 只验证玩法；无 scenario/纹理）。
		if (!_headless)
		{
			_sceneProjector = new SceneProjector();
			_tracker = new RenderSnapshotTracker();
			_renderGateway = new GodotRenderGateway();
			_renderGateway.Initialize(GetViewport().GetWorld3D().Scenario);
			_renderGateway.RegisterMeshColor(RenderSyncComponent.MeshPathPlayer, new Color(0.0f, 0.9f, 1.0f)); // 青：玩家
			_renderGateway.RegisterMeshColor(RenderSyncComponent.MeshPathEnemy, new Color(1.0f, 0.3f, 0.3f));  // 红：敌人
			_renderGateway.RegisterMeshColor(RenderSyncComponent.MeshPathBullet, new Color(1.0f, 0.9f, 0.2f)); // 黄：子弹

			// 相机：斜视俯看 XZ 平面；站 +Z 侧朝 -Z 看（Godot 惯例），-Z = 玩家前方（W=前进 / 空格子弹朝前一致）。
			var cam = new Camera3D { Fov = 60.0f, Near = 0.05f, Far = 200.0f };
			cam.Position = new Vector3(0, 16, 10);
			AddChild(cam);
			cam.LookAt(new Vector3(0, 0, 0), Vector3.Up);
			cam.MakeCurrent();
		}

		GD.Print($"shooter: 玩法对接启动（headless={_headless} WASD 移动 / 空格射击）");
	}

	public override void _Process(double delta)
	{
		_frameCount++;
		if (_world == null || _input == null || _inputState == null)
		{
			return;
		}

		// 1) 输入：gateway 采样 → InputState → O2 InputService（不 Reset WasPressed，边沿由 WeaponAction 维护）。
		_input.SampleFixed();
		var frame = _input.LastFrame() ?? InputFrame.Empty;
		_inputState.Frame = frame;
		var svc = _world.GetResource<InputService>()!;
		svc.MoveX = 0;
		svc.MoveZ = 0;
		svc.FirePressed = false;
		if (frame.Samples != null)
		{
			foreach (var s in frame.Samples)
			{
				switch (s.Name)
				{
					case "move_left": svc.MoveX -= s.AxisValue; break;
					case "move_right": svc.MoveX += s.AxisValue; break;
					case "move_forward": svc.MoveZ -= s.AxisValue; break; // -Z = 前方（Godot 惯例）
					case "move_back": svc.MoveZ += s.AxisValue; break;
					case "fire": svc.FirePressed = s.Pressed; break;
				}
			}
		}

		// 2) 渲染同步附着：有 Position 无 RenderSync 的对象（含新生成子弹）挂同步组件。
		foreach (var obj in _world.Roots)
		{
			if (obj.GetComponent<Position>() != null && obj.GetComponent<RenderSyncComponent>() == null)
			{
				obj.AddComponent<RenderSyncComponent>();
			}
		}

		// 3) 玩法权威一帧（O2：Move → Collide → world.Tick）。
		ShooterGame.RunFrame(_world, (float)delta);

		// 4) 渲染投影（真窗口；读最新 Transform）。
		if (!_headless && _sceneProjector != null && _tracker != null && _renderGateway != null)
		{
			_renderGateway.Consume(_tracker.Diff(_sceneProjector.Project(_world)));
		}

		// 5) 状态打印（变化时）。
		var match = _world.GetResource<MatchController>()!;
		if (match.Score != _lastScore || match.AliveEnemies != _lastAlive)
		{
			_lastScore = match.Score;
			_lastAlive = match.AliveEnemies;
			GD.Print($"shooter: frame={_frameCount} score={match.Score} alive={match.AliveEnemies} phase={match.Phase}");
		}
		if (match.Phase == GamePhase.GameOver && !_overReported)
		{
			_overReported = true;
			GD.Print($"shooter: 游戏结束 score={match.Score} alive={match.AliveEnemies}");
		}

		if (_frameCount == 480)
		{
			if (!_headless && _renderGateway != null)
			{
				var img = GetViewport().GetTexture().GetImage();
				string path = ProjectSettings.GlobalizePath("user://shooter.png");
				img.SavePng(path);
				GD.Print($"shooter: 截图 → user://shooter.png render={_renderGateway.DebugInfo}");
			}
			GD.Print("shooter: 演示结束（480 帧）");
			GetTree().Quit();
		}
	}

	public override void _ExitTree()
	{
		_renderGateway?.Dispose();
	}
}