// SPDX-License-Identifier: MIT
// GodotInputGateway.cs —— Godot 输入采集真桥（O8/M1 输入 Gateway）
//
// IInputGateway 的 Godot 实现：把 Godot 输入动作（InputMap）采样为 InputFrame，
// 由 Sola3dMainLoop 在 fixed 边界调用 SampleFixed 收集、注入 GameWorld Resources 端口。
// 动作名与 project.godot [input] 段一致（move_left/move_right/move_forward/move_back/fire）。

using Godot;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>Godot 输入采集：InputMap 动作 → InputSample 列表（fixed 边界采样）。</summary>
public sealed partial class GodotInputGateway : IInputGateway
{
	/// <summary>采集的动作名（与 project.godot [input] 对齐；新增动作在此登记）。</summary>
	public static readonly string[] Actions =
	{
		"move_left", "move_right", "move_forward", "move_back", "fire",
	};

	private readonly System.Collections.Generic.List<InputSample> _samples = new();
	private InputFrame _last = InputFrame.Empty;

	public void BeginFrame(float nowSeconds)
	{
	}

	public void EndFrame(float nowSeconds)
	{
	}

	/// <summary>一次 fixed 边界采样：读取全部动作当前状态 → 最近一帧。</summary>
	public void SampleFixed()
	{
		_samples.Clear();
		foreach (string action in Actions)
		{
			// 轴输入用 GetActionStrength（-1..1 语义由引擎映射）；按键输入用按下边沿。
			float strength = Input.GetActionStrength(action);
			bool pressed = Input.IsActionPressed(action);
			if (strength > 0.001f || pressed)
			{
				_samples.Add(new InputSample(action, axisValue: strength, pressed: pressed));
			}
		}
		_last = new InputFrame(0, _samples.ToArray());
	}

	public InputFrame? LastFrame() => _last;
}