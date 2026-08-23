// SPDX-License-Identifier: MIT
// InputFrame.cs —— 输入帧端口（O5，O5-GameWorldHost与ServerPorts.md §5）
//
// InputHost 采集平台输入 → InputFrame → 注入 GameWorld（Resources 端口，§11）。
// Gameplay 组件经 GetResource<InputState>() 读取，不直接摸平台 API。

using System.Collections.Generic;

namespace Sola3d.MainLoop;

/// <summary>单一输入样本（按键/轴/指针）。</summary>
public readonly record struct InputSample
{
	/// <summary>输入名（如 "move_forward"/"fire"，平台无关逻辑键名）。</summary>
	public string Name { get; init; }

	/// <summary>轴值（-1..1；非轴输入恒 0）。</summary>
	public float AxisValue { get; init; }

	/// <summary>是否按下（按键类）。</summary>
	public bool Pressed { get; init; }

	/// <summary>指针位置（可选；x=列 y=行）。</summary>
	public (float X, float Y)? Pointer { get; init; }

	public InputSample(string name, float axisValue = 0, bool pressed = false, (float X, float Y)? pointer = null)
	{
		Name = name;
		AxisValue = axisValue;
		Pressed = pressed;
		Pointer = pointer;
	}
}

/// <summary>一帧输入：fixed 边界采样（TickIndex 对齐确定性）。</summary>
public readonly record struct InputFrame(ulong TickIndex, IReadOnlyList<InputSample> Samples)
{
	/// <summary>空帧（无输入时的合法默认）。</summary>
	public static readonly InputFrame Empty = new(0, System.Array.Empty<InputSample>());
}
