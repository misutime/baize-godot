// SPDX-License-Identifier: MIT
// Main.cs —— Godot 场景 Composition Root；e2e 模式只负责安排门禁，不承载玩法规则

using Godot;

namespace Sola3d.GodotSlice;

public partial class Main : Node3D
{
	public override void _Ready()
	{
		if (IsPreviewMode())
		{
			// O7.5：隔离 ECS slice 内容，只显示我们的 GameObject+Component cube（经 Gateway 渲染）。
			// O7.5：隔离 ECS slice 内容，只显示我们的 GameObject+Component cube（经 Gateway 渲染）。
			if (GetNodeOrNull<Node>("EcsHost") is Node ecsHost)
			{
				ecsHost.ProcessMode = ProcessModeEnum.Disabled;
			}
			if (GetNodeOrNull<Node3D>("Presentation") is Node3D pres)
			{
				pres.Visible = false;
			}
			if (GetNodeOrNull<Node3D>("GridGround") is Node3D grid)
			{
				grid.Visible = false;
			}
			var demo = new DemoPreview();
			AddChild(demo);
			return;
		}
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

	private static bool IsPreviewMode()
	{
		foreach (string argument in OS.GetCmdlineUserArgs())
		{
			if (argument == "--preview3d") return true;
		}
		return false;
	}
}
