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
		world.AddService(new MatchState());
		world.AddService(new InputService());
		world.AddService(new SpawnConfig());
		world.AddService(new SpawnState());
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

/// <summary>一帧步进：Tick 后帧末统一 flush 延迟目标销毁（reviewer P1：同 Tick 多弹命中同一目标 → 帧末销毁）。</summary>
	public static void Step(GameWorld world, float delta = 0.01f)
	{
		world.Tick(delta);
		world.GetService<MatchState>().FlushFrame(world); // 帧末提交命中（GameOver 整帧丢弃）
	}
	/// <summary>重启一局：Reset 清空对象并归零 TickIndex，Services 保留，再重建场景。</summary>
	public static void Restart(GameWorld world)
	{
		world.Reset();
		world.GetService<MatchState>().Reset();
		world.GetService<InputService>().Reset();
		world.GetService<SpawnState>().Remaining = 0;
		SetupScene(world, withPlayer: true);
	}
}
