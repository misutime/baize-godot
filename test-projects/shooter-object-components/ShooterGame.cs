// SPDX-License-Identifier: MIT
// ShooterGame.cs —— O2 Composition Root：装配世界（服务 + 世界宿主 + 玩家）

using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>O2 Shooter 装配根。</summary>
public static class ShooterGame
{
	public static GameWorld CreateWorld(float fixedDelta = 0.01f, bool withPlayer = true)
	{
		var world = new GameWorld(fixedDelta);
		Install(world, withPlayer);
		return world;
	}

	public static void Install(GameWorld world, bool withPlayer = true)
	{
		// 全局服务：对局控制器（阶段/计分/冻结）、输入、生成配置、碰撞几何。
		var match = new MatchController();
		match.Bind(world);
		world.AddService(match);
		world.AddService(new InputService());
		world.AddService(new SpawnConfig());
		world.AddService(new SpawnState());
		world.AddService(new CollisionResolver());

		SetupScene(world, withPlayer);
	}

	private static void SetupScene(GameWorld world, bool withPlayer)
	{
		var host = world.CreateGameObject("Game");
		host.AddComponent<EnemySpawnerBehavior>();
		if (withPlayer)
		{
			ShooterFactory.SpawnPlayer(world, 0, 0);
		}
	}

	/// <summary>步进：控制器先提交本 tick 的不可变运动计划，再统一执行移动与碰撞。</summary>
	public static void Step(GameWorld world, float delta = 0.01f)
	{
		if (!world.Paused)
		{
			ulong tickIndex = world.TickIndex + 1;

			// 玩家先规划：敌人据玩家本帧终点寻敌，等价于玩家先移动，但不依赖实际 tick 顺序。
			foreach (var obj in ShooterWorld.QueryObjects(world, o => o.GetComponent<PlayerControllerBehavior>() != null))
			{
				obj.GetComponent<PlayerControllerBehavior>()!.PlanMotion(delta, tickIndex);
			}
			foreach (var obj in ShooterWorld.QueryObjects(world, o => o.GetComponent<EnemyControllerBehavior>() != null))
			{
				obj.GetComponent<EnemyControllerBehavior>()!.PlanMotion(delta, tickIndex);
			}
			foreach (var obj in ShooterWorld.QueryObjects(world, o => o.GetComponent<BulletBehavior>() != null))
			{
				obj.GetComponent<BulletBehavior>()!.PlanMotion(delta, tickIndex);
			}
		}

		world.Tick(delta);
	}

	public static void Restart(GameWorld world)
	{
		world.Reset();
		world.GetService<MatchController>().Reset();
		world.GetService<InputService>().Reset();
		world.GetService<SpawnState>().Remaining = 0;
		SetupScene(world, withPlayer: true);
	}
}
