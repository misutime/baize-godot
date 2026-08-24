// SPDX-License-Identifier: MIT
// Main.cs —— Godot 场景 Composition Root（O7.5 起：纯 Object 宿主，无 ECS）

using Godot;

namespace Sola3d.GodotSlice;

/// <summary>
/// 启动演示：默认 DemoPreview（O7.5 渲染链路）；--physics → PhysicsPreview（headless 物理 e2e）；
/// --physics-render → PhysicsRenderPreview（P0 物理→渲染联动，真窗口）；--input → InputPreview（输入 Gateway）。
/// ECS 验证路径已迁移出 godot-slice（旧 --e2e/HeadlessE2e 删除；ECS 对照走模块测试）。
/// </summary>
public partial class Main : Node3D
{
	public override void _Ready()
	{
		var userArgs = OS.GetCmdlineUserArgs();
		bool physics = false;
		bool physicsRender = false;
		bool input = false;
		foreach (var arg in userArgs)
		{
			if (arg == "--physics")
			{
				physics = true;
			}
			else if (arg == "--physics-render")
			{
				physicsRender = true;
			}
			else if (arg == "--input")
			{
				input = true;
			}
		}
		if (input)
		{
			GD.Print("godot-slice: Sola3d Object 宿主启动（--input 输入 Gateway 演示）");
			AddChild(new InputPreview());
		}
		else if (physicsRender)
		if (physicsRender)
		{
			GD.Print("godot-slice: Sola3d Object 宿主启动（--physics-render 物理→渲染联动演示）");
			AddChild(new PhysicsRenderPreview());
		}
		else if (physics)
		{
			GD.Print("godot-slice: Sola3d Object 宿主启动（--physics 物理域第一刀演示）");
			AddChild(new PhysicsPreview());
		}
		else
		{
			GD.Print("godot-slice: Sola3d Object 宿主启动（--preview3d 演示）");
			AddChild(new DemoPreview());
		}
	}
}
