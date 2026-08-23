// SPDX-License-Identifier: MIT
// CameraPresenter.cs —— 只跟随 RenderSnapshot 中的玩家（与玩家同一插值链路），不访问或修改 ECS 组件
//
// 玩家在 RenderAdapter 用 PreviousSnapshot→CurrentSnapshot + physics interpolation fraction
// 插值；相机必须用同一插值，否则渲染帧落在两个 physics tick 之间时相机会跳到 current 而玩家仍在插值位置，
// 造成相对位置每 Tick 抖动。因此这里也读双快照并插值玩家位置。

using Godot;
using Shooter.Gameplay;

namespace Sola3d.GodotSlice;

public partial class CameraPresenter : Node3D
{
	[Export] public NodePath HostPath { get; set; } = new("../../EcsHost");
	[Export] public NodePath CameraPath { get; set; } = new("Camera3D");

	private EcsHost? _host;
	private Camera3D? _camera;

	public override void _Ready()
	{
		_host = GetNodeOrNull<EcsHost>(HostPath);
		_camera = GetNode<Camera3D>(CameraPath);
		_camera.Current = true;
	}

	public override void _Process(double delta)
	{
		if (_host is null || _camera is null) return;

		RenderSnapshot previous = _host.PreviousSnapshot.Render;
		RenderSnapshot current = _host.CurrentSnapshot.Render;
		if (current.Players.Length == 0) return;

		// 与 RenderAdapter 相同的 physics interpolation fraction + 双快照插值
		float alpha = Mathf.Clamp((float)Engine.GetPhysicsInterpolationFraction(), 0, 1);
		RenderEntitySnapshot player = InterpolatePlayer(previous, current, alpha);
		Vector3 target = new(player.X, 0, player.Z);
		_camera.GlobalPosition = target + new Vector3(0, 18, 14);
		_camera.LookAt(target, Vector3.Up);
	}

	private static RenderEntitySnapshot InterpolatePlayer(
		RenderSnapshot previous, RenderSnapshot current, float alpha)
	{
		RenderEntitySnapshot item = current.Players[0];
		RenderEntitySnapshot from = previous.TryFind(item, out RenderEntitySnapshot found)
			? found : item;
		RenderEntitySnapshot interpolated = item with
		{
			X = Mathf.Lerp(from.X, item.X, alpha),
			Z = Mathf.Lerp(from.Z, item.Z, alpha),
		};
		return interpolated;
	}
}
