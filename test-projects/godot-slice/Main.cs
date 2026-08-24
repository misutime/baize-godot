// SPDX-License-Identifier: MIT
// Main.cs —— Godot 场景 Composition Root（O7.5 起：纯 Object 宿主，无 ECS）

using Godot;

namespace Sola3d.GodotSlice;

/// <summary>
/// 唯一职责：启动 DemoPreview（Object+Component → Gateway → RenderingServer 真实窗口演示）。
/// ECS 验证路径已迁移出 godot-slice（旧 --e2e/HeadlessE2e 删除；ECS 对照走模块测试）。
/// </summary>
public partial class Main : Node3D
{
	public override void _Ready()
	{
		GD.Print("godot-slice: Sola3d Object 宿主启动（--preview3d 演示）");
		var demo = new DemoPreview();
		AddChild(demo);
	}
}
