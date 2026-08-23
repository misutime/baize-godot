// SPDX-License-Identifier: MIT
// ShooterGame.cs —— O2 Composition Root：装配世界（资源 + 世界宿主 + 玩家）

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
		// 全局资源：对局控制器（阶段/计分）、多来源暂停、输入、生成配置、碰撞几何。
		world.AddResource(new MatchController());
		world.AddResource(new PauseManager());
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

	/// <summary>跑一帧：先汇聚暂停（终局/菜单等来源）→ Move 阶段（玩家→敌人→子弹到本帧终点）→ Collide 阶段（子弹扫掠命中）→ 杂项 OnTick。</summary>
	public static void RunFrame(GameWorld world, float delta = 0.01f)
	{
		ApplyPause(world);
		if (!world.Paused)
		{
			// 阶段1 Move：所有"会动"的对象先移动到本帧终点（移动阶段全部先于碰撞 → 顺序无关）。
			ShooterWorldHelper.MoveAll(world, delta);
			// Move 阶段可能触发终局（敌人接触）→ 当帧立即冻结，需再次汇聚。
			ApplyPause(world);
		}
		if (!world.Paused)
		{
			// 阶段2 Collide：子弹做扫掠命中（读双方本帧 prev→pos）。
			ShooterWorldHelper.CollideAll(world, delta);
		}
		world.Tick(delta);
	}

	public static void Restart(GameWorld world)
	{
		world.Reset();
		world.GetResource<MatchController>().Reset();
		world.GetResource<InputService>().Reset();
		world.GetResource<SpawnState>().Remaining = 0;
		world.GetResource<PauseManager>().Clear();
		SetupScene(world, withPlayer: true);
	}

	/// <summary>汇聚多来源暂停 → 写入 O1 门禁。组合根是唯一聚合点：任一来源（PauseManager 菜单/暂停表 + 终局 Phase）处于暂停即 world.Paused=true。</summary>
	private static void ApplyPause(GameWorld world)
	{
		var match = world.GetResource<MatchController>();
		var pause = world.GetResource<PauseManager>();
		world.Paused = pause.IsPaused || match.Phase == GamePhase.GameOver;
	}
}
