// SPDX-License-Identifier: MIT
// Program.cs —— vertical-slice-tests：O6 最小垂直切片验证（纯 .NET headless）
//
// 断言：① 数据组件生命周期（Add/Get/Remove）② 含 Vector3 属性组件往返（R27）③ SceneProjector
// 从世界投影出 RenderCommand ④ Gateway 消费命令不反写 Gameplay（单向）。

using System;
using System.Collections.Generic;
using System.Numerics;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace VerticalSliceTests;

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

	/// <summary>渲染命令负载（Projector → Gateway）。</summary>
	private sealed record RenderCommand : GatewayCommand
	{
		public ObjectId ObjectId { get; init; }
		public string MeshPath { get; init; } = "";
		public Vector3 Position { get; init; }
		public Vector3 Scale { get; init; }
		public Quaternion Rotation { get; init; }
	}

	/// <summary>世界 → CommandBus 投影器（读 Transform/Mesh 语义，产 RenderCommand）。</summary>
	private sealed class SceneProjector
	{
		public int Project(GameWorld world, CommandBus commands)
		{
			int count = 0;
			foreach (var root in world.Roots)
			{
				count += Walk(root, world, commands);
			}
			return count;
		}

		private int Walk(GameObject obj, GameWorld world, CommandBus commands)
		{
			int count = 0;
			var tf = obj.GetComponent<TransformComponent>();
			var mesh = obj.GetComponent<MeshComponent>();
			if (tf != null && mesh != null)
			{
				commands.Push(new RenderCommand
				{
					ObjectId = obj.Id,
					MeshPath = mesh.MeshPath,
					Position = tf.Position,
					Scale = tf.Scale,
					Rotation = tf.Rotation,
				});
				count++;
			}
			foreach (var child in obj.Children)
			{
				count += Walk(child, world, commands);
			}
			return count;
		}
	}

	/// <summary>headless 假渲染后端：记录收到的命令（验证投影流）。</summary>
	private sealed class FakeRenderGateway : IRenderGateway
	{
		public readonly List<RenderCommand> Received = new();

		public void BeginFrame(float nowSeconds) { }
		public void EndFrame(float nowSeconds) { }

		public void Consume(IReadOnlyList<GatewayCommand> commands)
		{
			foreach (var c in commands)
			{
				if (c is RenderCommand rc)
				{
					Received.Add(rc);
				}
			}
		}
	}

	private static void Test_组件生命周期()
	{
		var world = new GameWorld();
		world.Schemas.Register<TransformComponent>();
		world.Schemas.Register<MeshComponent>();
		world.Schemas.Register<StaticColliderComponent>();

		var cube = world.CreateGameObject("Cube");
		var tf = cube.AddComponent<TransformComponent>();
		tf.Position = new Vector3(1, 2, 3);
		tf.Scale = new Vector3(2, 2, 2);
		cube.AddComponent<MeshComponent>().MeshPath = "res://primitive/cube.mesh";
		cube.AddComponent<StaticColliderComponent>().BoxSize = new Vector3(1, 1, 1);

		Check("组件：Add 后可 Get", cube.GetComponent<TransformComponent>() != null);
		Check("组件：值保留", cube.GetComponent<TransformComponent>()!.Position == new Vector3(1, 2, 3));
		Check("组件：三个组件都在", cube.Components.Count >= 3);
	}

	private static void Test_Vector3序列化往返()
	{
		var world = new GameWorld();
		world.Schemas.Register<TransformComponent>();
		var cube = world.CreateGameObject("Cube");
		var tf = cube.AddComponent<TransformComponent>();
		tf.Position = new Vector3(1.5f, -2.25f, 3.125f);
		tf.Rotation = Quaternion.CreateFromYawPitchRoll(0.5f, 0.25f, 0.125f);

		var snap = GameWorldSerializer.Capture(world);
		string text = GameWorldTextSerializer.Serialize(snap);
		var parsed = GameWorldTextSerializer.Deserialize(text);
		var restored = GameWorldSerializer.Restore(parsed, world.Schemas, world.Relations);

		Check("R27：含 Vector3 组件往返 hash 相等",
			GameWorldSerializer.ComputeHash(GameWorldSerializer.Capture(restored)) == GameWorldSerializer.ComputeHash(snap));
		var rt = restored.Roots[0].GetComponent<TransformComponent>()!;
		Check("R27：Position 往返保真", rt.Position == tf.Position);
		Check("R27：Rotation 往返保真", rt.Rotation == tf.Rotation);
		Console.WriteLine($"      文本示例：{text.Trim().Split('\n')[^1]}"); // 显示 Position 行格式
	}

	private static void Test_投影路径()
	{
		var world = new GameWorld();
		world.Schemas.Register<TransformComponent>();
		world.Schemas.Register<MeshComponent>();
		var loop = new Sola3dMainLoop(new Driver(world));
		var render = new FakeRenderGateway();
		loop.AddGateway(render);

		var cube = world.CreateGameObject("Cube");
		cube.AddComponent<TransformComponent>().Position = new Vector3(0, 5, 0);
		cube.AddComponent<MeshComponent>().MeshPath = "res://models/cube.mesh";

		// Projector 每帧投影 → CommandBus → Gateway 消费。
		var projector = new SceneProjector();
		int projected = projector.Project(world, loop.Commands);
		var drained = loop.Commands.Drain();
		render.Consume(drained);

		Check("投影：Cube 被投影为 1 命令", projected == 1 && drained.Count == 1);
		Check("投影：Gateway 收到 1 命令", render.Received.Count == 1);
		Check("投影：位置正确", render.Received[0].Position == new Vector3(0, 5, 0));
		Check("投影：mesh 路径正确", render.Received[0].MeshPath == "res://models/cube.mesh");
	}

	private static void Test_投影单向不反写()
	{
		var world = new GameWorld();
		world.Schemas.Register<TransformComponent>();
		world.Schemas.Register<MeshComponent>();
		var cube = world.CreateGameObject("Cube");
		var tf = cube.AddComponent<TransformComponent>();
		tf.Position = new Vector3(1, 1, 1);
		cube.AddComponent<MeshComponent>();

		var loop = new Sola3dMainLoop(new Driver(world));
		var projector = new SceneProjector();
		var original = tf.Position;

		// Gateway 消费后位置不变（单向投影）。
		var render = new FakeRenderGateway();
		loop.AddGateway(render);
		projector.Project(world, loop.Commands);
		render.Consume(loop.Commands.Drain());

		Check("单向：Gateway 消费不反写 Gameplay", tf.Position == original && render.Received[0].Position == original);
	}

	/// <summary>极简 IWorldDriver（本测试只需 loop 存在；tick 不进投影路径）。</summary>
	private sealed class Driver : IWorldDriver
	{
		private readonly GameWorld _world;
		public Driver(GameWorld world) => _world = world;
		public float FixedDelta => _world.FixedDelta;
		public float NowSeconds { get; private set; }
		public void FixedTick() { _world.FixedTick(_world.FixedDelta); NowSeconds += _world.FixedDelta; }
		public void Tick(float delta) { _world.Tick(delta); NowSeconds += delta; }
		public void InjectInput(InputFrame frame) { }
	}

	private static int Main()
	{
		Console.WriteLine("vertical-slice-tests —— O6 最小垂直切片（Transform/Mesh/StaticCollider Gateway 投影）\n");

		Test_组件生命周期();
		Test_Vector3序列化往返();
		Test_投影路径();
		Test_投影单向不反写();

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
