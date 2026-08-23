// SPDX-License-Identifier: MIT
// Program.cs —— mainloop-core-tests：O5 MainLoop/Gateway 抽象层契约验证（headless 纯 .NET）
//
// 断言：双轨判帧、InputFrame 注入、Port 三通道（Event/Command/Observation）、
// 确定性保持（fixed 边界采样）、Gateway 生命周期序。

using System;
using System.Collections.Generic;
using Sola3d.MainLoop;

namespace MainLoopCoreTests;

internal static class Program
{
	private static int _passed;
	private static readonly List<string> _failed = new();

	private static void Check(string name, bool condition)
	{
		if (condition)
		{
			_passed++;
		}
		else
		{
			_failed.Add(name);
			Console.WriteLine($"[失败] {name}");
		}
	}

	// ---------- 模拟世界驱动（记录 tick 计数与注入帧） ----------

	private sealed class CountingWorld : IWorldDriver
	{
		public float FixedDelta { get; } = 0.02f;
		public float NowSeconds { get; private set; }
		public int FixedTickCount;
		public int VariableTickCount;
		public readonly List<InputFrame> Injected = new();

		public void FixedTick()
		{
			FixedTickCount++;
			NowSeconds += FixedDelta;
		}

		public void Tick(float delta)
		{
			VariableTickCount++;
			NowSeconds += delta;
		}

		public void InjectInput(InputFrame frame) => Injected.Add(frame);
	}

	// ---------- 模拟输入宿主 ----------

	private sealed class FakeInputGateway : IInputGateway
	{
		private InputFrame _frame = InputFrame.Empty;

		public void BeginFrame(float nowSeconds) { }
		public void EndFrame(float nowSeconds) { }

		public void SampleFixed() { }

		/// <summary>测试注入合成输入。</summary>
		public void SetFrame(InputFrame frame) => _frame = frame;

		public InputFrame? LastFrame() => _frame;
	}

	// ---------- 测试 ----------

	private static void Test_双轨判帧()
	{
		var world = new CountingWorld();
		var loop = new Sola3dMainLoop(world);
		int fixedIndex = 0;
		loop.Observations.Subscribe(o => fixedIndex++);

		// FixedDelta=0.02：每帧 0.05s → 每帧 2 次 fixed + 1 次 variable。
		loop.Frame(0.05f);
		Check("双轨：0.05s → 2 fixed + 1 variable", world.FixedTickCount == 2 && world.VariableTickCount == 1);
		loop.Frame(0.05f);
		Check("双轨：累计 0.10s → 5 fixed + 2 variable（0.10/0.02=5）", world.FixedTickCount == 5 && world.VariableTickCount == 2);
		// 长时间帧：0.061 → 3 次 fixed（0.02*3=0.06 ≤ 0.061）。
		loop.Frame(0.061f);
		Check("双轨：累计 0.161s → 8 fixed（0.161/0.02=8）", world.FixedTickCount == 8 && world.VariableTickCount == 3);
		// 小帧：0.01 不够一次 fixed → 0 fixed。
		loop.Frame(0.01f);
		Check("双轨：0.01s 不足步长 → fixed 不变（8）且 variable +1", world.FixedTickCount == 8 && world.VariableTickCount == 4);
	}

	private static void Test_输入注入()
	{
		var world = new CountingWorld();
		var loop = new Sola3dMainLoop(world);
		var input = new FakeInputGateway();
		loop.AddGateway(input);

		// 注入合成输入：fixed 边界采样。
		input.SetFrame(new InputFrame(1, new[] { new InputSample("fire", pressed: true) }));
		loop.Frame(0.05f);
		Check("输入：每 fixed tick 注入一帧（2 fixed → 至少 2 帧注入）", world.Injected.Count >= 2);
		Check("输入：注入帧含 fire 样本", world.Injected[0].Samples.Count == 1 && world.Injected[0].Samples[0].Name == "fire");

		// variable 域也注入最新帧。
		Check("输入：variable 帧注入最新", world.Injected[^1].Samples.Count == 1);
	}

	private static void Test_Port三通道()
	{
		var world = new CountingWorld();
		var loop = new Sola3dMainLoop(world);

		// Event：Gateway 发布 → Gameplay 在 tick 边界 Drain。
		loop.Events.Publish(new TestEvent { AtTickIndex = 1 });
		Check("Event：发布后待消费", loop.Events.Count == 1);
		// 帧处理内不自动消费（Gameplay 在 tick 边界 Drain）——此处直接验证 Drain 语义。
		var drained = loop.Events.Drain();
		Check("Event：Drain 消费并清空", drained.Count == 1 && loop.Events.Count == 0);

		// Command：Gameplay 下发 → Gateway 帧末 Drain。
		loop.Commands.Push(new TestCommand());
		Check("Command：入队", loop.Commands.Count == 1);
		var commands = loop.Commands.Drain();
		Check("Command：消费清空", commands.Count == 1 && loop.Commands.Count == 0);

		// Observation：Gateway 提交 → 本帧统一分发（Sola3dMainLoop.Frame 调 Dispatch）。
		bool received = false;
		loop.Observations.Subscribe(o => received = o is TestObservation);
		loop.Observations.Submit(new TestObservation { AtTickIndex = 2 });
		loop.Frame(0.02f);
		Check("Observation：帧末分发", received && loop.Observations.Count == 0);
	}

	private static void Test_Gateway生命周期序()
	{
		var world = new CountingWorld();
		var loop = new Sola3dMainLoop(world);
		var log = new List<string>();
		loop.AddGateway(new LogGateway(log, "A"));
		loop.AddGateway(new LogGateway(log, "B"));
		loop.Frame(0.02f);
		Check("Gateway 序：Begin 按注册序", log[0] == "A.begin" && log[1] == "B.begin");
		Check("Gateway 序：End 按注册序（帧末）", log[^2] == "A.end" && log[^1] == "B.end");
	}

	private sealed class LogGateway : IGateway
	{
		private readonly List<string> _log;
		private readonly string _name;
		public LogGateway(List<string> log, string name) { _log = log; _name = name; }
		public void BeginFrame(float nowSeconds) => _log.Add(_name + ".begin");
		public void EndFrame(float nowSeconds) => _log.Add(_name + ".end");
	}

	private sealed record TestEvent : GameplayEvent { }
	private sealed record TestCommand : GatewayCommand { }
	private sealed record TestObservation : GatewayObservation { }

	private static void Test_Cube几何()
	{
		var verts = Sola3d.Host.CubeGeometry.Vertices;
		var idx = Sola3d.Host.CubeGeometry.Indices;
		Check("Cube：24 顶点（每面 4 独立法线）", verts.Length == 24);
		Check("Cube：36 索引（12 三角形）", idx.Length == 36);
		bool allInRange = true;
		foreach (var i in idx)
		{
			if (i < 0 || i >= 24) { allInRange = false; break; }
		}
		Check("Cube：索引范围合法（0..23）", allInRange);
		// 法线：6 个面各 4 顶点同法线（硬边）。
		bool normPerFace = true;
		for (int f = 0; f < 6; f++)
		{
			var n = Sola3d.Host.CubeGeometry.Normals[f * 4];
			for (int k = 1; k < 4; k++)
			{
				if (Sola3d.Host.CubeGeometry.Normals[f * 4 + k] != n) { normPerFace = false; break; }
			}
		}
		Check("Cube：每面 4 顶点同法线（硬边）", normPerFace);
	}
	private static int Main()
	{
		Console.WriteLine("mainloop-core-tests —— O5 MainLoop/Gateway 抽象层契约验证\n");

		Test_双轨判帧();
		Test_输入注入();
		Test_Port三通道();
		Test_Gateway生命周期序();
		Test_Cube几何();
		Console.WriteLine($"\n通过 {_passed} 项，失败 {_failed.Count} 项");
		if (_failed.Count > 0)
		{
			Console.WriteLine("失败清单：" + string.Join(", ", _failed));
			return 1;
		}
		Console.WriteLine("全部通过 ✅");
		return 0;
	}
}
