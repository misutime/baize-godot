// SPDX-License-Identifier: MIT
// Sola3dMainLoop.cs —— 进程宿主抽象（O5，O5-GameWorldHost与ServerPorts.md）
//
// Runtime World 的进程外壳：GameWorld 挂载 + 双轨 tick 驱动 + Host 注册 + Port 通道。
// 设计依据（社区对照）：Bevy MainSchedule/FixedMain 双轨节奏、Unity PlayerLoopSystem 子系统分层、
//                       Dom Williams sim/render 分离 + Port 隔离。
// 零 Godot 依赖：GameWorld 经 IWorldDriver 解耦，本层 headless 可测、可服务器复用。

using System;
using System.Collections.Generic;

namespace Sola3d.Host;

/// <summary>
/// 世界驱动接口：隔离 Host 层与具体世界实现（GameWorld 适配器在引用 Sola3d.GameObject 的层实现）。
/// fixed 步进、variable 步进、输入注入——Host 层只做"到点推进 + 注入"，语义全在实现方。
/// </summary>
public interface IWorldDriver
{
	/// <summary>固定步长（物理权威域，O5 起）。</summary>
	float FixedDelta { get; }

	/// <summary>推进一帧 fixed tick（fixed 边界采样输入；FixedTickIndex 单调）。</summary>
	void FixedTick();

	/// <summary>推进一帧 variable tick（游戏逻辑；delta 渲染色帧）。</summary>
	void Tick(float delta);

	/// <summary>注入输入帧（GameWorld 经 Resources 端口读取，§11）。</summary>
	void InjectInput(InputFrame frame);

	/// <summary>当前累计时间（帧判频用，服务器/客户端同节奏）。</summary>
	float NowSeconds { get; }
}

/// <summary>
/// 进程宿主：Runtime World 外壳。驱动固定步长 + 变步长双轨，按累计时间判帧
/// （Bevy RunFixedMainLoop 同构），注册 Host 集合（Unity PlayerLoopSystem 同构）。
/// </summary>
public sealed class Sola3dMainLoop
{
	private readonly IWorldDriver _world;
	private readonly List<IHost> _hosts = new();
	private readonly EventBus _events = new();
	private readonly CommandBus _commands = new();
	private readonly ObservationBus _observations = new();
	private float _accumulator;
	private float _now;

	/// <summary>事件通道（Backend → Gameplay：碰撞/命中/UI 点击）。</summary>
	public EventBus Events => _events;

	/// <summary>命令通道（Gameplay → Backend：画 Mesh/注册 Collider）。</summary>
	public CommandBus Commands => _commands;

	/// <summary>观察通道（Backend → Gameplay：Physics 权威位姿回传）。</summary>
	public ObservationBus Observations => _observations;

	public Sola3dMainLoop(IWorldDriver world)
	{
		_world = world ?? throw new ArgumentNullException(nameof(world));
	}

	/// <summary>注册 Host（有序；执行序 = 注册序，Unity PlayerLoopSystem 同构）。</summary>
	public void AddHost(IHost host)
	{
		ArgumentNullException.ThrowIfNull(host);
		_hosts.Add(host);
	}

	/// <summary>推进一帧（Godot MainLoop._Process 或服务器循环调用）。</summary>
	public void Frame(float delta)
	{
		if (delta < 0)
		{
			throw new ArgumentOutOfRangeException(nameof(delta), "delta 必须非负。");
		}
		_now += delta;
		_accumulator += delta;

		// 每帧先让 Host 采样（InputHost 收集 → 放入暂存）。
		foreach (var host in _hosts)
		{
			host.BeginFrame(_now);
		}

		// fixed 域：累计时间驱动的固定步子循环（Bevy RunFixedMainLoop 同构）。
		while (_accumulator >= _world.FixedDelta)
		{
			// 输入在 fixed 边界采样并注入（§14.6：fixed tick 边界收集）。
			var frame = BuildInputFrame();
			foreach (var host in _hosts)
			{
				if (host is IInputHost input)
				{
					input.SampleFixed();
				}
			}
			_world.InjectInput(frame);
			_world.FixedTick();
			_accumulator -= _world.FixedDelta;
		}

		// variable 域：每帧一次（游戏逻辑；delta 为渲染色帧）。
		var vframe = BuildInputFrame();
		_world.InjectInput(vframe);
		_world.Tick(delta);

		// 观察回传：Backend → Gameplay（fixed 边界收集，本帧统一分发）。
		_observations.Dispatch();

		// 每帧末：Host 收尾。
		foreach (var host in _hosts)
		{
			host.EndFrame(_now);
		}
	}

	/// <summary>构建输入帧：InputHost 提供合成输入；无 Host 时为默认空帧。</summary>
	private InputFrame BuildInputFrame()
	{
		var samples = new List<InputSample>();
		ulong tickIndex = 0;
		foreach (var host in _hosts)
		{
			if (host is IInputHost input)
			{
				var f = input.LastFrame();
				if (f.HasValue)
				{
					samples.AddRange(f.Value.Samples);
					tickIndex = f.Value.TickIndex;
				}
			}
		}
		return new InputFrame(tickIndex, samples);
	}
}

/// <summary>Host 基类接口：每帧生命周期钩子（BeginFrame → [SampleFixed] → EndFrame）。</summary>
public interface IHost
{
	/// <summary>帧开始（Host 可在这里做平台事件轮询）。</summary>
	void BeginFrame(float nowSeconds);

	/// <summary>帧结束（Host 收尾，如提交渲染命令）。</summary>
	void EndFrame(float nowSeconds);
}

/// <summary>输入宿主：采集平台输入 → 生成 InputFrame（sola3d 输入端口）。</summary>
public interface IInputHost : IHost
{
	/// <summary>一次 fixed 边界采样（headless 下测试注入合成帧）。</summary>
	void SampleFixed();

	/// <summary>最近一帧输入（供 Sola3dMainLoop 注入）。</summary>
	InputFrame? LastFrame();
}

/// <summary>窗口宿主（O6 实现；O5 壳）。</summary>
public interface IWindowHost : IHost
{
}

/// <summary>渲染世界宿主（RenderingServer/RID；O6 实现，O5 壳）。</summary>
public interface IRenderWorldHost : IHost
{
}

/// <summary>物理世界宿主（PhysicsServer/Jolt；O6 实现，O5 壳）。</summary>
public interface IPhysicsWorldHost : IHost
{
}

/// <summary>最小 UI 宿主（O5 留接口；O8 域迁移）。</summary>
public interface IUIHost : IHost
{
}
