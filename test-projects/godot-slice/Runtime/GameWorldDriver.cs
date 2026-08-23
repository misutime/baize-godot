// SPDX-License-Identifier: MIT
// GameWorldDriver.cs —— IWorldDriver 适配（O5，O5-GameWorldHost与ServerPorts.md）
//
// 把 Sola3d.MainLoop 的 IWorldDriver 桥到 Sola3d.GameObject.GameWorld：
// fixed/variable tick 转发 + InputFrame 注入 Resources 端口（§11）。
// 纯 .NET（零 Godot 依赖）——本文件可 headless 测试，也是服务器/编辑器预览复用的实现。

using System;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>GameWorld 输入状态（Resources 端口承载；Gameplay 组件经 GetResource 读取）。</summary>
public sealed class InputState
{
	/// <summary>最近注入的输入帧（fixed 边界，确定性对齐）。</summary>
	public InputFrame Frame { get; set; } = InputFrame.Empty;
}

/// <summary>IWorldDriver → GameWorld 适配器。</summary>
public sealed class GameWorldDriver : IWorldDriver
{
	private readonly GameWorld _world;
	private readonly InputState _inputState;
	private float _now;

	public GameWorldDriver(GameWorld world)
	{
		_world = world ?? throw new ArgumentNullException(nameof(world));
		_inputState = new InputState();
		if (!world.HasResource<InputState>())
		{
			world.AddResource(_inputState);
		}
		else
		{
			_inputState = world.GetResource<InputState>()!;
		}
	}

	public float FixedDelta => _world.FixedDelta;

	public float NowSeconds => _now;

	/// <summary>世界实例（Host 之外的直接访问入口）。</summary>
	public GameWorld World => _world;

	public void FixedTick()
	{
		// 契约：FixedTick 忽略入参 delta，固定用 world.FixedDelta。
		_world.FixedTick(_world.FixedDelta);
		_now += _world.FixedDelta;
	}

	public void Tick(float delta)
	{
		_world.Tick(delta);
		_now += delta;
	}

	public void InjectInput(InputFrame frame)
	{
		_inputState.Frame = frame;
	}
}
