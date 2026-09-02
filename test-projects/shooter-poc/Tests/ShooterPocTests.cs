// SPDX-License-Identifier: MIT
// ShooterPocTests.cs —— 可执行验收：测试安排与游戏 Composition Root 明确分离

using System;
using System.Collections.Generic;
using Baize.Ecs;
using Friflo.Engine.ECS;
using Shooter.Gameplay;
using Position = Shooter.Gameplay.Position;
namespace ShooterPoc;

internal static class ShooterPocTests
{
	// 回放脚本仍固定向 +Z 射击；全向生成后不保证命中，计分规则由专用命中测试覆盖。
	private const int ExpectedScriptScore = 0;
	private static int _failures;

	public static int RunAll()
	{
		_failures = 0;
		RunDeterministicTest();
		RunSpawnCoverageTest();
		RunFireEdgeTest();
		RunScoreAndDuplicateHitTest();
		RunSweptTrajectoryTest();
		RunGameOverFreezeTest();
		RunEntityReuseTest();
		RunResetTest();

		Console.WriteLine($"shooter-poc: 测试完成, failures={_failures}");
		if (_failures != 0) return 1;

		Console.WriteLine("shooter-poc: 验证成功——Human-first Authoring 玩法闭环可用");
		return 0;
	}

	private static void RunDeterministicTest()
	{
		RunResult first = RunOnce();
		RunResult second = RunOnce();
		Console.WriteLine($"shooter-poc: 确定性 hash={first.Hash} vs {second.Hash}, " +
						  $"score={first.Score}/{second.Score}, phase={first.Phase}/{second.Phase}");

		Check(first.Hash == second.Hash, "确定性回放 state hash 不一致");
		Check(first.Score == second.Score && first.Phase == second.Phase,
			"确定性回放最终状态不一致");
		Check(first.Score == ExpectedScriptScore,
			$"脚本场景预期得分 {ExpectedScriptScore}，实际 {first.Score}");
		Check(first.Phase == GamePhase.GameOver,
			$"脚本场景预期 GameOver，实际 {first.Phase}");
		Check(first.ProjectileCount <= 20,
			$"投射物无限增长（{first.ProjectileCount}）");
	}

	private static void RunSpawnCoverageTest()
	{
		EcsWorld world = CreateWorld(spawnInterval: 0, maxAlive: 64);
		var seen = new HashSet<int>();
		bool positiveX = false;
		bool negativeX = false;
		bool positiveZ = false;
		bool negativeZ = false;

		for (int tick = 0; tick < 66; tick++)
		{
			world.Tick(InputFrame.Empty);
			foreach (Entity entity in world.Store.Query<Position>()
				.AllTags(Tags.Get<EnemyFaction>()).Entities)
			{
				if (!seen.Add(entity.Id)) continue;
				ref Position position = ref entity.GetComponent<Position>();
				if (MathF.Abs(position.X) >= MathF.Abs(position.Z))
				{
					if (position.X >= 0) positiveX = true;
					else negativeX = true;
				}
				else
				{
					if (position.Z >= 0) positiveZ = true;
					else negativeZ = true;
				}
			}
		}

		Console.WriteLine($"shooter-poc: 生成方向 +X={positiveX}, -X={negativeX}, " +
			$"+Z={positiveZ}, -Z={negativeZ}, 敌人={seen.Count}");
		Check(seen.Count == 64, $"期望生成 64 个敌人，实际 {seen.Count}");
		Check(positiveX && negativeX && positiveZ && negativeZ,
			"确定性生成序列未覆盖四个方向");
	}

	private static RunResult RunOnce()
	{
		EcsWorld world = CreateWorld();
		ulong hash = 1469598103934665603UL;
		foreach (InputFrame frame in BuildScriptFrames())
		{
			world.Tick(frame);
			Mix(ref hash, ComputeStateHash(world));
		}

		MatchState match = world.GetState<MatchState>();
		return new RunResult(unchecked((long)hash), match.Score, match.Phase,
			CountEntitiesWithTag<ProjectileTag>(world));
	}

	private static void RunFireEdgeTest()
	{
		EcsWorld world = CreateWorld(maxAlive: 0);
		SetPlayerCooldown(world, 0);
		InputFrame[] frames =
		[
			new(0, 0, false),
			new(0, 0, true),
			new(0, 0, true),
			new(0, 0, false),
			new(0, 0, true),
			new(0, 0, false), // 播放上一 Tick 的第二个生成命令
		];
		foreach (InputFrame frame in frames) world.Tick(frame);

		int projectileCount = CountEntitiesWithTag<ProjectileTag>(world);
		Console.WriteLine($"shooter-poc: Fire 边沿 投射物={projectileCount}");
		Check(projectileCount == 2,
			$"Fire 边沿期望生成 2 个投射物，实际 {projectileCount}");
	}

	private static void RunScoreAndDuplicateHitTest()
	{
		EcsWorld world = CreateWorld(maxAlive: 0);
		AddEnemy(world, 0, 2, moveSpeed: 0);
		AddProjectile(world, -0.1f, 0, 0, 120);
		AddProjectile(world, 0.1f, 0, 0, 120);

		StepEmpty(world, 3);
		MatchState match = world.GetState<MatchState>();
		int enemies = CountEntitiesWithTag<EnemyFaction>(world);
		Console.WriteLine($"shooter-poc: 多命中 score={match.Score}, 敌人={enemies}");

		Check(match.Score == 1, $"同 Tick 多次命中应只得 1 分，实际 {match.Score}");
		Check(enemies == 0, $"命中后敌人应删除，实际 {enemies}");
		Check(match.AliveEnemies == 0,
			$"AliveEnemies 应归零，实际 {match.AliveEnemies}");
	}

	private static void RunSweptTrajectoryTest()
	{
		EcsWorld futureWorld = CreateWorld(maxAlive: 0);
		AddEnemy(futureWorld, 0, 1.8f, moveSpeed: 0);
		AddProjectile(futureWorld, 0, 0, 0, 60);
		StepEmpty(futureWorld, 2);
		Check(futureWorld.GetState<MatchState>().Score == 0,
			"swept 错把 current→future 当作本 Tick 轨迹");
		futureWorld.Tick(InputFrame.Empty);
		Check(futureWorld.GetState<MatchState>().Score == 1,
			"swept previous→current 未在真实穿越 Tick 命中");

		EcsWorld radiusWorld = CreateWorld(maxAlive: 0);
		AddEnemy(radiusWorld, 0.65f, 1, moveSpeed: 0, radius: 0.5f);
		AddProjectile(radiusWorld, 0, 0, 0, 60, radius: 0.2f);
		StepEmpty(radiusWorld, 2);
		int radiusScore = radiusWorld.GetState<MatchState>().Score;
		Console.WriteLine($"shooter-poc: swept previous→current + 半径和 score={radiusScore}");
		Check(radiusScore == 1, "swept 未使用敌人半径 + 投射物半径");
	}

	private static void RunGameOverFreezeTest()
	{
		EcsWorld world = CreateWorld(spawnInterval: 0, maxAlive: 10);
		AddEnemy(world, 0, 0, moveSpeed: 0);

		world.Tick(InputFrame.Empty);
		world.Tick(new InputFrame(1, 0, true));
		MatchState match = world.GetState<MatchState>();
		Check(match.Phase == GamePhase.GameOver,
			$"接触敌人后应 GameOver，实际 {match.Phase}");

		Position frozenPosition = GetPlayerPosition(world);
		int frozenEnemyCount = CountEntitiesWithTag<EnemyFaction>(world);
		int frozenProjectileCount = CountEntitiesWithTag<ProjectileTag>(world);

		for (int i = 0; i < 8; i++)
		{
			world.Tick(new InputFrame(1, 1, i % 2 == 0));
		}

		Position finalPosition = GetPlayerPosition(world);
		int finalEnemyCount = CountEntitiesWithTag<EnemyFaction>(world);
		int finalProjectileCount = CountEntitiesWithTag<ProjectileTag>(world);
		Console.WriteLine($"shooter-poc: GameOver 冻结 pos=({finalPosition.X},{finalPosition.Z}), " +
						  $"敌人={finalEnemyCount}, 投射物={finalProjectileCount}");

		Check(finalPosition.X == frozenPosition.X && finalPosition.Z == frozenPosition.Z,
			"GameOver 后玩家位置仍变化");
		Check(finalEnemyCount == frozenEnemyCount, "GameOver 后仍生成/删除敌人");
		Check(finalProjectileCount == frozenProjectileCount,
			"GameOver 后仍落地排队投射物或清理投射物");
		Check(match.AliveEnemies == finalEnemyCount,
			$"GameOver 后 AliveEnemies={match.AliveEnemies} 与实体数={finalEnemyCount} 不一致");
	}

	private static void RunEntityReuseTest()
	{
		EcsWorld world = CreateWorld(maxAlive: 0);
		Entity source = AddProjectile(world, -10, 0, 0, 0);
		Entity oldTarget = AddEnemy(world, 10, 0, moveSpeed: 0);

		oldTarget.DeleteEntity();
		world.GetState<MatchState>().AliveEnemies = 0;

		Entity newTarget = AddEnemy(world, 10, 0, moveSpeed: 0, entityId: oldTarget.Id);
		world.Events.Writer<DamageRequested>()
			.Send(new DamageRequested(source, oldTarget, 1));
		world.Tick(InputFrame.Empty);

		MatchState match = world.GetState<MatchState>();
		bool reused = oldTarget.Id == newTarget.Id && oldTarget.Revision != newTarget.Revision;
		bool replacementsAlive = !source.IsNull && !newTarget.IsNull;
		Console.WriteLine($"shooter-poc: ID 复用 source={source}, " +
						  $"target old={oldTarget}, new={newTarget}, score={match.Score}");

		Check(reused, "测试未形成相同 Id、不同 Revision 的真实复用场景");
		Check(match.Score == 0, $"旧代际请求误计分，实际 {match.Score}");
		Check(replacementsAlive, "旧代际请求误删了复用 Id 的新实体");
	}

	private static void RunResetTest()
	{
		EcsWorld world = CreateWorld(maxAlive: 0);
		SetPlayerCooldown(world, 0);
		world.Tick(InputFrame.Empty);
		world.Tick(new InputFrame(0, 0, true));
		world.Tick(new InputFrame(0, 0, false));
		Check(CountEntitiesWithTag<ProjectileTag>(world) == 1,
			"Reset 前未建立输入边沿运行状态");

		world.Reset();
		world
			.InsertState(new MatchState())
			.InsertState(new SpawnState())
			.InsertState(new FireInputState());

		MatchState match = world.GetState<MatchState>();
		int entityCount = CountAllEntities(world);
		Console.WriteLine($"shooter-poc: Reset 后 score={match.Score}, 实体={entityCount}, " +
						  $"Tick={world.TickIndex}, FireWasPressed={world.GetState<FireInputState>().WasPressed}");

		Check(match.Score == 0 && match.AliveEnemies == 0
							   && match.Phase == GamePhase.Playing,
			"Reset 后 MatchState 未归零");
		Check(entityCount == 0, $"Reset 后全部实体应为 0，实际 {entityCount}");
		Check(world.TickIndex == 0, $"Reset 后 Tick 应为 0，实际 {world.TickIndex}");
		Check(!world.GetState<FireInputState>().WasPressed,
			"Reset 后 FireInputState 未归零");
	}

	private static EcsWorld CreateWorld(
		float spawnInterval = 1.0f, int maxAlive = 10, float spawnRadius = 20.0f)
	{
		var world = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
		ShooterGame.Install(world);

		SpawnConfig config = world.GetState<SpawnConfig>();
		config.Interval = spawnInterval;
		config.MaxAlive = maxAlive;
		config.SpawnRadius = spawnRadius;
		return world;
	}

	private static Entity AddEnemy(EcsWorld world, float x, float z,
		float moveSpeed, float radius = 0.5f, int? entityId = null)
	{
		var bundle = new EnemyBundle(x, z, moveSpeed, radius: radius);
		Entity entity = entityId.HasValue
			? world.SpawnNow(entityId.Value, bundle)
			: world.SpawnNow(bundle);
		world.GetState<MatchState>().AliveEnemies++;
		return entity;
	}

	private static Entity AddProjectile(EcsWorld world, float x, float z,
		float velocityX, float velocityZ, float radius = 0.2f)
	{
		return world.SpawnNow(new ProjectileBundle(
			x, z, velocityX, velocityZ, radius: radius));
	}

	private static void SetPlayerCooldown(EcsWorld world, float cooldown)
	{
		foreach (var entity in world.Store.Query<WeaponConfig>()
					 .AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			ref WeaponConfig weapon = ref entity.GetComponent<WeaponConfig>();
			weapon.CooldownSeconds = cooldown;
		}
	}

	private static Position GetPlayerPosition(EcsWorld world)
	{
		foreach (var entity in world.Store.Query<Position>()
					 .AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			return entity.GetComponent<Position>();
		}

		throw new InvalidOperationException("玩家实体不存在");
	}

	private static void StepEmpty(EcsWorld world, int count)
	{
		for (int i = 0; i < count; i++) world.Tick(InputFrame.Empty);
	}

	private static InputFrame[] BuildScriptFrames()
	{
		var frames = new InputFrame[600];
		for (int i = 0; i < frames.Length; i++)
		{
			float moveX = i >= 100 && i < 200 ? 1 : 0;
			bool fire = i < 240 && i % 30 == 10;
			frames[i] = new InputFrame(moveX, 0, fire);
		}
		return frames;
	}

	private static ulong ComputeStateHash(EcsWorld world)
	{
		ulong hash = 1469598103934665603UL;
		MatchState match = world.GetState<MatchState>();
		Mix(ref hash, (long)world.TickIndex);
		Mix(ref hash, (int)match.Phase);
		Mix(ref hash, match.Score);
		Mix(ref hash, match.AliveEnemies);

		SpawnConfig config = world.GetState<SpawnConfig>();
		Mix(ref hash, config.Interval);
		Mix(ref hash, config.MaxAlive);
		Mix(ref hash, config.SpawnRadius);
		Mix(ref hash, world.GetState<SpawnState>().Remaining);
		Mix(ref hash, world.GetState<FireInputState>().WasPressed ? 1 : 0);

		var entities = new List<Entity>();
		foreach (Entity entity in world.Store.Entities) entities.Add(entity);
		entities.Sort((a, b) => a.Id != b.Id
			? a.Id.CompareTo(b.Id)
			: a.Revision.CompareTo(b.Revision));

		Mix(ref hash, entities.Count);
		foreach (Entity entity in entities)
		{
			Mix(ref hash, entity.Id);
			Mix(ref hash, entity.Revision);
			Mix(ref hash, entity.Tags.Has<PlayerFaction>() ? 1 : 0);
			Mix(ref hash, entity.Tags.Has<EnemyFaction>() ? 1 : 0);
			Mix(ref hash, entity.Tags.Has<ProjectileTag>() ? 1 : 0);
			HashComponents(ref hash, entity);
		}
		return hash;
	}

	private static void HashComponents(ref ulong hash, Entity entity)
	{
		if (entity.HasComponent<Position>())
		{
			Mix(ref hash, 101); ref Position value = ref entity.GetComponent<Position>();
			Mix(ref hash, value.X); Mix(ref hash, value.Z);
		}
		if (entity.HasComponent<PreviousPosition>())
		{
			Mix(ref hash, 102); ref PreviousPosition value = ref entity.GetComponent<PreviousPosition>();
			Mix(ref hash, value.X); Mix(ref hash, value.Z);
		}
		if (entity.HasComponent<Velocity>())
		{
			Mix(ref hash, 103); ref Velocity value = ref entity.GetComponent<Velocity>();
			Mix(ref hash, value.X); Mix(ref hash, value.Z);
		}
		if (entity.HasComponent<Health>())
		{
			Mix(ref hash, 104); ref Health value = ref entity.GetComponent<Health>();
			Mix(ref hash, value.Current); Mix(ref hash, value.Max);
		}
		if (entity.HasComponent<MoveSpeed>())
		{
			Mix(ref hash, 105); ref MoveSpeed value = ref entity.GetComponent<MoveSpeed>();
			Mix(ref hash, value.Value);
		}
		if (entity.HasComponent<WeaponConfig>())
		{
			Mix(ref hash, 106); ref WeaponConfig value = ref entity.GetComponent<WeaponConfig>();
			Mix(ref hash, value.CooldownSeconds); Mix(ref hash, value.ProjectileSpeed);
		}
		if (entity.HasComponent<Cooldown>())
		{
			Mix(ref hash, 107); ref Cooldown value = ref entity.GetComponent<Cooldown>();
			Mix(ref hash, value.Remaining);
		}
		if (entity.HasComponent<CollisionRadius>())
		{
			Mix(ref hash, 108); ref CollisionRadius value = ref entity.GetComponent<CollisionRadius>();
			Mix(ref hash, value.Value);
		}
		if (entity.HasComponent<ProjectileConfig>())
		{
			Mix(ref hash, 109); ref ProjectileConfig value = ref entity.GetComponent<ProjectileConfig>();
			Mix(ref hash, value.Damage); Mix(ref hash, value.MaxRange);
		}
		if (entity.HasComponent<TravelDistance>())
		{
			Mix(ref hash, 110); ref TravelDistance value = ref entity.GetComponent<TravelDistance>();
			Mix(ref hash, value.Value);
		}
		if (entity.HasComponent<PlayerInput>()) Mix(ref hash, 111);
		if (entity.HasComponent<SeekTarget>()) Mix(ref hash, 112);
	}

	private static void Mix(ref ulong hash, long value)
	{
		unchecked
		{
			hash ^= (ulong)value;
			hash *= 1099511628211UL;
		}
	}

	private static void Mix(ref ulong hash, float value) =>
		Mix(ref hash, BitConverter.SingleToInt32Bits(value));

	private static int CountEntitiesWithTag<T>(EcsWorld world) where T : struct, ITag
	{
		int count = 0;
		foreach (Entity entity in world.Store.Entities)
		{
			if (entity.Tags.Has<T>()) count++;
		}
		return count;
	}

	private static int CountAllEntities(EcsWorld world)
	{
		int count = 0;
		foreach (Entity _ in world.Store.Entities) count++;
		return count;
	}

	private static void Check(bool condition, string failure)
	{
		if (condition) return;
		Console.WriteLine($"FAIL: {failure}");
		_failures++;
	}

	private readonly struct RunResult(
		long hash, int score, GamePhase phase, int projectileCount)
	{
		public long Hash { get; } = hash;
		public int Score { get; } = score;
		public GamePhase Phase { get; } = phase;
		public int ProjectileCount { get; } = projectileCount;
	}
}
