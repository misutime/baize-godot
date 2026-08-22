// SPDX-License-Identifier: MIT
// InputAdapter.cs —— Godot InputMap 到纯数据 InputFrame 的唯一翻译边界

using Baize.Ecs;
using Godot;

namespace Baize.GodotSlice;

public partial class InputAdapter : Node
{
	private static readonly StringName MoveLeft = new("move_left");
	private static readonly StringName MoveRight = new("move_right");
	private static readonly StringName MoveForward = new("move_forward");
	private static readonly StringName MoveBack = new("move_back");
	private static readonly StringName Fire = new("fire");

	public InputFrame Capture()
	{
		Vector2 movement = Input.GetVector(MoveLeft, MoveRight, MoveForward, MoveBack);
		return new InputFrame(
			movement.X,
			-movement.Y,
			Input.IsActionPressed(Fire),
			0,
			1);
	}
}
