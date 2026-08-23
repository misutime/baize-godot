// SPDX-License-Identifier: MIT
// ShooterGame.cs —— O2 Composition Root：装配世界（资源 + 世界宿主 + 玩家）

using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>O2 Shooter 装配根。</summary>
public static class ShooterGame
{
	/// <summary>规划阶段唯一权威顺序（PlayerInput → Enemy → Projectile）。</summary>
	private static readonly PlanPhase[] _planPhases =
		{ PlanPhase.PlayerInput, PlanPhase.Enemy, PlanPhase.Projectile };

	public static GameWorld CreateWorld(float fixedDelta = 0.01f, bool withPlayer = true)
	{
		var world = new GameWorld(fixedDelta);
		Install(world, withPlayer);
		return world;
	}

	public static void Install(GameWorld world, bool withPlayer = true)
	{
		// 全局资源：对局控制器（阶段/计分/冻结）、输入、生成配置、碰撞几何。
		var match = new MatchController();
		match.Bind(world);
		world.AddResource(match);
		world.AddResource(new InputService());
		world.AddResource(new SpawnConfig());
		world.AddResource(new SpawnState());
		world.AddResource(new CollisionResolver());

		SetupScene(world, withPlayer);
	}

	private static void SetupScene(GameWorld world, bool withPlayer)
	{
		var host = world.CreateGameObject("Game");
		host.AddComponent<EnemySpawnerAction>();
		if (withPlayer)
		{
			ShooterFactory.SpawnPlayer(world, 0, 0);
		}
	}

	/// <summary>跑一帧：控制器先提交本 tick 的不可变运动计划，再统一执行移动与碰撞。</summary>
	public static void RunFrame(GameWorld world, float delta = 0.01f)
	{
		if (!world.Paused)
		{
			ulong tickIndex = world.TickIndex + 1;

			// 规划阶段按 PlanPhase 声明序执行（PlayerInput → Enemy → Projectile）：
			// 玩家先规划，敌人据玩家本帧终点寻敌，子弹提交自身线段——顺序即游戏语义，不依赖 tick 顺序。
			foreach (var phase in _planPhases)
			{
				ShooterWorldHelper.PlanMotion(world, delta, tickIndex, phase);
			}
		}


		world.Tick(delta);
	}

	public static void Restart(GameWorld world)
	{
		world.Reset();
		world.GetResource<MatchController>().Reset();
		world.GetResource<InputService>().Reset();
		world.GetResource<SpawnState>().Remaining = 0;
		SetupScene(world, withPlayer: true);
	}
}
