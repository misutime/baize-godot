// SPDX-License-Identifier: MIT
// Main.cs —— Godot 场景 Composition Root；e2e 模式只负责安排门禁，不承载玩法规则

using Godot;

namespace Baize.GodotSlice;

public partial class Main : Node3D
{
	public override void _Ready()
	{
		if (!IsE2eMode())
		{
			GD.Print("godot-slice: P2.3 Godot vertical slice 已启动");
			return;
		}

		EcsHost host = GetNode<EcsHost>("EcsHost");
		host.SetPhysicsProcess(false);

		Node? presentation = GetNodeOrNull<Node>("Presentation");
		bool hadRenderNodes = presentation is not null
			&& HasNode("Presentation/RenderAdapter/Enemies")
			&& HasNode("Presentation/RenderAdapter/Projectiles")
			&& HasNode("Presentation/Hud/HudPresenter");
		if (presentation is not null)
		{
			RemoveChild(presentation);
			presentation.Free();
		}

		bool presentationWasDeleted = hadRenderNodes && !HasNode("Presentation");
		int failures = HeadlessE2e.Run(host, presentationWasDeleted);
		GetTree().Quit(failures == 0 ? 0 : 1);
	}

	private static bool IsE2eMode()
	{
		foreach (string argument in OS.GetCmdlineUserArgs())
		{
			if (argument == "--e2e") return true;
		}
		return false;
	}
}
