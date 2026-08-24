// SPDX-License-Identifier: MIT
// InputPreview.cs —— 输入 Gateway 最小演示（O8/M1；headless 可自动验证）
//
// 链路：GodotInputGateway 采样（fixed 边界，Sola3dMainLoop 驱动）→ InputFrame
// 注入 GameWorld Resources（GameWorldDriver）→ 本演示读 InputState 打印。
// headless：无输入 → samples=0（自动断言）；真窗口：用户按键（WASD/空格）→ 动作名打印。

using Godot;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>输入采集链路演示：gateway → InputFrame → GameWorld InputState 资源。</summary>
public sealed partial class InputPreview : Node3D
{
	private GameWorld? _world;
	private Sola3dMainLoop? _loop;
	private GodotInputGateway? _input;
	private int _frameCount;
	private int _lastSampleCount = -1;

	public override void _Ready()
	{
		_world = new GameWorld();
		var driver = new GameWorldDriver(_world);
		_loop = new Sola3dMainLoop(driver);
		_input = new GodotInputGateway();
		_loop.AddGateway(_input);
		GD.Print("input: 演示启动（WASD 移动 / 空格 fire；headless 无输入 → 空帧）");
	}

	public override void _Process(double delta)
	{
		_frameCount++;
		_loop?.Frame((float)delta);

		// 读 GameWorldDriver 注入的 InputState（Resources 端口）。
		var state = _world?.GetResource<InputState>();
		if (state != null)
		{
			var samples = state.Frame.Samples;
			int count = samples?.Count ?? 0;
			if (_frameCount <= 3 || count != _lastSampleCount)
			{
				_lastSampleCount = count;
				string names = count == 0 ? "-" : string.Join(",", samples!);
				GD.Print($"input: tick={state.Frame.TickIndex} samples={count} [{names}]");
			}
		}

		if (_frameCount == 360)
		{
			GD.Print("input: 演示结束（360 帧）");
			GetTree().Quit();
		}
	}
}