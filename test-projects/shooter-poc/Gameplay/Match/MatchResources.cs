// SPDX-License-Identifier: MIT
// MatchResources.cs —— 一局游戏的声明式状态与全局运行数据

using Baize.Ecs;

namespace ShooterPoc;

public enum GamePhase { Playing, GameOver }

// EcsState 同时是 Resource；Phase 只能通过 TransitionTo 改变。
public sealed class MatchState : EcsState<GamePhase>
{
	public MatchState() : base(GamePhase.Playing) { }

	public GamePhase Phase => Current;
	public int Score;
	public int AliveEnemies;

	protected override void OnExit(EcsWorld world, GamePhase state)
	{
		if (state == GamePhase.Playing)
		{
			// 离开 Playing 时丢弃尚未落地的射击与生成命令。
			world.CommandBuffer.Reset();
		}
	}

	protected override void OnEnter(EcsWorld world, GamePhase state)
	{
		if (state == GamePhase.Playing)
		{
			// 新一局从干净的输入边沿与生成节拍开始。
			if (world.WorldState.Get<FireInputState>() is { } fire) fire.WasPressed = false;
			if (world.WorldState.Get<SpawnState>() is { } spawn) spawn.Remaining = 0;
			return;
		}

		// 进入 GameOver 自动冻结现存对象；各 System 无需重复判断 Phase。
		AliveEnemies = 0;
		foreach (var entity in world.Store.Entities)
		{
			if (entity.Tags.Has<EnemyFaction>()) AliveEnemies++;
			if (!entity.HasComponent<Velocity>()) continue;

			ref Velocity velocity = ref entity.GetComponent<Velocity>();
			velocity.X = 0;
			velocity.Z = 0;
		}
	}
}

