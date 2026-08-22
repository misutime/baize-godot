// SPDX-License-Identifier: MIT
// CameraPresenter.cs —— 只跟随 RenderSnapshot 中的玩家，不访问或修改 ECS 组件

using Godot;
using Shooter.Gameplay;

namespace Baize.GodotSlice;

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
		RenderSnapshot current = _host.CurrentSnapshot.Render;
		if (current.Players.Length == 0) return;

		RenderEntitySnapshot player = current.Players[0];
		Vector3 target = new(player.X, 0, player.Z);
		GlobalPosition = target + new Vector3(0, 18, 14);
		LookAt(target, Vector3.Up);
	}
}
