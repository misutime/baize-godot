// SPDX-License-Identifier: MIT
// EcsHost.cs —— 只负责 Godot 生命周期、输入交接与 ECS 固定 Tick，不拥有表现节点

using Baize.Ecs;
using Friflo.Engine.ECS;
using Godot;
using Shooter.Gameplay;

namespace Baize.GodotSlice;

public partial class EcsHost : Node
{
	[Export]
	public NodePath InputAdapterPath { get; set; } = new("InputAdapter");

	private InputAdapter? _inputAdapter;

	public EcsWorld World { get; private set; } = null!;
	public ShooterFrameSnapshot PreviousSnapshot { get; private set; } = ShooterFrameSnapshot.Empty;
	public ShooterFrameSnapshot CurrentSnapshot { get; private set; } = ShooterFrameSnapshot.Empty;

	public override void _Ready()
	{
		_inputAdapter = GetNodeOrNull<InputAdapter>(InputAdapterPath);
		InitializeWorld();
	}

	private void InitializeWorld()
	{
		World = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
		ShooterGame.Install(World);

		ShooterFrameSnapshot initial = World.GetState<ShooterSnapshotState>().Current;
		PreviousSnapshot = initial;
		CurrentSnapshot = initial;
	}

	public void Reset()
	{
		World?.Dispose();
		InitializeWorld();
	}

	public override void _UnhandledInput(InputEvent @event)
	{
		if (@event is not InputEventKey keyEvent || !keyEvent.Pressed || keyEvent.Echo) return;
		if (keyEvent.PhysicalKeycode != Key.R && keyEvent.Keycode != Key.R) return;
		if (CurrentSnapshot.Hud.Phase != GamePhase.GameOver) return;

		Reset();
		GetViewport().SetInputAsHandled();
	}

	public override void _PhysicsProcess(double delta)
	{
		InputFrame input = _inputAdapter?.Capture() ?? InputFrame.Empty;
		Step(input);
	}

	public void Step(in InputFrame input)
	{
		PreviousSnapshot = CurrentSnapshot;
		World.Tick(input);
		CurrentSnapshot = World.GetState<ShooterSnapshotState>().Current;
	}

	public override void _ExitTree()
	{
		World?.Dispose();
	}
}
