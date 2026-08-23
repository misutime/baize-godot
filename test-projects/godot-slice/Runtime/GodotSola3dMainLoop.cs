// SPDX-License-Identifier: MIT
// GodotSola3dMainLoop.cs —— Godot 进程入口适配壳（O5，O5-GameWorldHost与ServerPorts.md §2）
//
// 参考社区：Godot 官方支持自定义 MainLoop（SceneTree 只是默认实现，可自定义 C# 子类）；
// headless 检测（DisplayServer.get_name()=="headless"）→ 纯逻辑跑（服务器/CLI）。
// 桥接：Godot MainLoop 每帧回调 → Sola3d.Host.Sola3dMainLoop.Frame。

using Godot;
using Sola3d.Host;

namespace Sola3d.GodotSlice;

/// <summary>
/// Godot 进程入口：自定义 MainLoop 取代 SceneTree 作为语义入口（§15.5），
/// 内部驱动 Sola3dMainLoop（GameWorld + Host + Port）。
/// </summary>
public sealed partial class GodotSola3dMainLoop : MainLoop
{
	private readonly Sola3dMainLoop _loop;

	public GodotSola3dMainLoop(Sola3dMainLoop loop)
	{
		_loop = loop ?? throw new System.ArgumentNullException(nameof(loop));
	}

	public override void _Initialize()
	{
		bool headless = DisplayServer.GetName() == "headless";
		GD.Print($"godot-slice: Sola3dMainLoop 启动（headless={headless}）");
	}

	public override bool _Process(double delta)
	{
		_loop.Frame((float)delta);
		return false; // false = 继续运行（返回 true 退出进程）
	}

	public override void _Finalize()
	{
		GD.Print("godot-slice: Sola3dMainLoop 结束");
	}
}
