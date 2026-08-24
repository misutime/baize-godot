// SPDX-License-Identifier: MIT
// GodotSola3dMainLoop.cs —— Godot 进程入口适配壳（O5，O5-GameWorldHost与ServerPorts.md §2）
//
// 参考社区：Godot 官方支持自定义 MainLoop（SceneTree 只是默认实现，可自定义 C# 子类）；
// headless 检测（DisplayServer.get_name()=="headless"）→ 纯逻辑跑（服务器/CLI）。
// 桥接：Godot MainLoop 每帧回调 → Sola3d.MainLoop.Sola3dMainLoop.Frame。

using Godot;
using GameLoopNs = Sola3d.MainLoop;
namespace Sola3d.GodotSlice;

/// <summary>
/// Godot 进程入口：自定义 MainLoop 取代 SceneTree 作为语义入口（§15.5），
/// 内部驱动 Sola3dMainLoop（GameWorld + Host + Port）。
/// </summary>
[GlobalClass]
public sealed partial class GodotSola3dMainLoop : Godot.MainLoop
{
	private GameLoopNs.Sola3dMainLoop? _loop;

	/// <summary>由 Godot 通过 application/run/main_loop_type 无参构造。</summary>
	public GodotSola3dMainLoop()
	{
	}

	public override void _Initialize()
	{
		bool headless = DisplayServer.GetName() == "headless";
		var world = new Sola3d.GameObject.GameWorld();
		var driver = new GameWorldDriver(world);
		_loop = new GameLoopNs.Sola3dMainLoop(driver);
		GD.Print($"godot-slice: Sola3dMainLoop 启动（headless={headless}，fixed_delta={world.FixedDelta:0.###}）");
	}

	public override bool _Process(double delta)
	{
		_loop?.Frame((float)delta);
		return false; // false = 继续运行（返回 true 退出进程）
	}

	public override void _Finalize()
	{
		_loop = null;
		GD.Print("godot-slice: Sola3dMainLoop 结束");
	}
}
