// SPDX-License-Identifier: MIT
// HeadlessE2e.cs —— Godot 进程内注入 InputFrame，验证完整玩法与表现隔离门禁

using Baize.Ecs;
using Friflo.Engine.ECS;
using Godot;
using Shooter.Gameplay;
using Position = Shooter.Gameplay.Position;

namespace Baize.GodotSlice;

internal static class HeadlessE2e
{
	public static int Run(EcsHost host, bool presentationWasDeleted)
	{
		int failures = 0;
		EcsWorld world = host.World;
		SpawnConfig spawn = world.GetState<SpawnConfig>();
		spawn.MaxAlive = 0;

		foreach (Entity entity in world.Store.Query<WeaponConfig>()
			.AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			ref WeaponConfig weapon = ref entity.GetComponent<WeaponConfig>();
			weapon.CooldownSeconds = 0;
		}

		host.Step(InputFrame.Empty);
		host.Step(new InputFrame(0, 0, true));
		host.Step(InputFrame.Empty);
		failures += Check(CountWithTag<ProjectileTag>(world) >= 1, "射击未生成投射物");
		if (failures == 0) GD.Print("godot-slice: 射击通过 [P23_FIRE_PASS]");

		int scoreBefore = world.GetState<MatchState>().Score;
		world.SpawnNow(new EnemyBundle(0, 2, moveSpeed: 0));
		world.GetState<MatchState>().AliveEnemies++;
		world.SpawnNow(new ProjectileBundle(0, 0, 0, 120));
		StepEmpty(host, 3);

		MatchState match = world.GetState<MatchState>();
		int scoreFailures = 0;
		scoreFailures += Check(match.Score == scoreBefore + 1,
			$"命中后应加 1 分，实际 {match.Score - scoreBefore}");
		scoreFailures += Check(CountWithTag<EnemyFaction>(world) == 0, "命中后敌人仍存在");
		failures += scoreFailures;
		if (scoreFailures == 0) GD.Print("godot-slice: 命中与计分通过 [P23_SCORE_PASS]");

		Position player = GetPlayerPosition(world);
		world.SpawnNow(new EnemyBundle(player.X, player.Z, moveSpeed: 0));
		match.AliveEnemies++;
		StepEmpty(host, 2);

		int deathFailures = Check(match.Phase == GamePhase.GameOver,
			$"敌人接触后应 GameOver，实际 {match.Phase}");
		failures += deathFailures;
		if (deathFailures == 0) GD.Print("godot-slice: 死亡通过 [P23_DEATH_PASS]");

		int isolationFailures = 0;
		isolationFailures += Check(presentationWasDeleted, "表现节点未被真实删除");
		isolationFailures += Check(host.CurrentSnapshot.Hud.Phase == GamePhase.GameOver,
			"删除表现节点后 Snapshot 未继续推进");
		isolationFailures += Check(host.CurrentSnapshot.Hud.TickIndex == world.TickIndex,
			"删除表现节点后宿主快照与 ECS Tick 不一致");
		failures += isolationFailures;
		if (isolationFailures == 0) GD.Print("godot-slice: 表现节点删除后模拟独立通过 [P23_ISOLATION_PASS]");

		if (failures == 0)
		{
			GD.Print("godot-slice: P2.3 vertical slice 验证成功 [P23_SLICE_PASS]");
		}
		else
		{
			GD.PushError($"godot-slice: P2.3 e2e 失败，failures={failures}");
		}
		return failures;
	}

	private static int Check(bool condition, string failure)
	{
		if (condition) return 0;
		GD.PushError($"godot-slice: FAIL: {failure}");
		return 1;
	}

	private static void StepEmpty(EcsHost host, int count)
	{
		for (int index = 0; index < count; index++) host.Step(InputFrame.Empty);
	}

	private static Position GetPlayerPosition(EcsWorld world)
	{
		foreach (Entity entity in world.Store.Query<Position>()
			.AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			return entity.GetComponent<Position>();
		}
		throw new System.InvalidOperationException("玩家实体不存在");
	}

	private static int CountWithTag<T>(EcsWorld world) where T : struct, ITag
	{
		int count = 0;
		foreach (Entity entity in world.Store.Entities)
		{
			if (entity.Tags.Has<T>()) count++;
		}
		return count;
	}
}
