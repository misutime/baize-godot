// SPDX-License-Identifier: MIT
// Program.cs —— O2 Go 版 Shooter 验收：干净 GameObject-first 玩法断言（对应 ECS shooter-poc 语义）

using System;
using System.Collections.Generic;
using Baize.GameObject;
using Shooter.Objects;

namespace ShooterObjectsPoc;

internal static class Program
{
	private static int _failures;

	private static void Check(string name, bool ok)
	{
		if (!ok)
		{
			Console.WriteLine($"FAIL: {name}");
			_failures++;
		}
	}

	private static void CheckEqu(string name, float actual, float expected, float eps = 1e-4f)
	{
		Check(name, MathF.Abs(actual - expected) <= eps);
		if (MathF.Abs(actual - expected) > eps)
		{
			Console.WriteLine($"     实际={actual} 期望={expected}");
		}
	}

	private static int Main()
	{
		Console.WriteLine("shooter-object-components-poc —— O2 GameObject-first 玩法验收\n");

		TestPlayerMovement();
		TestFireEdge();
		TestProjectileHitsEnemy();
		TestOrderIndependence();
		TestMotionPlanMatchesActual();
		TestDisabledActionStationary();
		TestDisabledPlayerNoMove();
		TestSmallSweepCrossing();
		TestNonLethalDamage();
		TestSpawnCoverage();
		TestEnemySeek();
		TestGameOverFreeze();
		TestStaleHandleNoResolution();
		TestRestart();
		TestDeterminism();

		Console.WriteLine($"shooter-object-components-poc: failures={_failures}");
		if (_failures != 0)
		{
			return 1;
		}
		Console.WriteLine("O2 验收通过：组件即能力——命中直接调用、阶段用 Paused 冻结、无帧末缓冲/仲裁器。");
		return 0;
	}

	// ---------- 1：玩家输入驱动移动 ----------

	private static void TestPlayerMovement()
	{
		var world = ShooterGame.CreateWorld();
		var input = world.GetService<InputService>();

		ShooterGame.Step(world);
		var player = FindPlayer(world)!;
		Check("移动：初始位置 (0,0)", player.GetComponent<Position>()!.X == 0 && player.GetComponent<Position>()!.Z == 0);

		input.MoveX = 1;
		ShooterGame.Step(world);
		CheckEqu("移动：+X 移动 0.08", player.GetComponent<Position>()!.X, 0.08f);

		input.MoveX = 0;
		ShooterGame.Step(world);
		CheckEqu("移动：停输入后位置不变", player.GetComponent<Position>()!.X, 0.08f);
	}

	// ---------- 2：Fire 边沿只发一弹 ----------

	private static void TestFireEdge()
	{
		var world = ShooterGame.CreateWorld();
		var input = world.GetService<InputService>();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		SetPlayerCooldownZero(world);

		input.FirePressed = false;
		ShooterGame.Step(world);
		input.FirePressed = true;
		ShooterGame.Step(world); // 边沿 → 1 弹
		ShooterGame.Step(world); // 保持按下，非边沿 → 0 弹
		input.FirePressed = false;
		ShooterGame.Step(world);
		input.FirePressed = true;
		ShooterGame.Step(world); // 新边沿 → 1 弹

		int count = CountWith<ProjectileTag>(world);
		Check($"Fire 边沿：共 {count} 弹（期望 2）", count == 2);
	}

	// ---------- 3：命中敌人死亡/计分 ----------

	private static void TestProjectileHitsEnemy()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");
		var match = world.GetService<MatchController>();
		ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 0);
		match.AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 30);

		for (int i = 0; i < 20 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}

		Check("命中：敌人被消灭", CountWith<EnemyFaction>(world) == 0);
		Check("命中：计分+1", match.Score == 1);
		Check("命中：弹已消费", CountWith<ProjectileTag>(world) == 0);
		Check("命中：AliveEnemies 归零", match.AliveEnemies == 0);
	}

	// ---------- 3b：创建顺序无关命中（子弹先建、敌人在其飞行路径后建 → 仍命中）----------

	private static void TestOrderIndependence()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		var match = world.GetService<MatchController>();
		// 子弹先创建（pre-existing）。但敌人作为高速「移动目标」在子弹路径上横穿——本帧内从 z=2 冲向 z=0，
		// 子弹 +Z 从 z=0 冲到 z=1——两者在同一 tick 内扫掠相交（t=2/3 处二者同在 z=2/3，距离 0）。
		// 若碰撞读取敌方实时 prev/pos，会在子弹先执行时看到敌人停在 z=2 而漏判；
		// 双方都消费 tick 前冻结的 MotionPlan，因此命中与实际执行顺序无关。
		// 玩家移到 z=-5：敌人冲向量不触及玩家（接触半径 1.0），无 GameOver 干扰。
		var player = FindPlayer(world)!;
		player.GetComponent<Position>()!.Z = -5f;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 100);
		var fastEnemy = ShooterFactory.SpawnEnemy(world, 0, 2.0f, moveSpeed: 200.0f, health: 1);
		match.AliveEnemies = 1;

		for (int i = 0; i < 10 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}


		Check("顺序无关：高速移动目标一 tick 内被命中", CountWith<EnemyFaction>(world) == 0);
		Check("顺序无关：命中计分+1", match.Score == 1);
		Check("顺序无关：玩家未被接触（无 GameOver）", match.Phase == GamePhase.Playing);
	}

	// ---------- 3c：计划必须等于实际移动（玩家本帧移动后，敌人按玩家终点重新寻向）----------

	private static void TestMotionPlanMatchesActual()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");
		var player = FindPlayer(world)!;
		player.GetComponent<MoveSpeed>()!.Value = 1000f;
		world.GetService<InputService>().MoveX = 1f; // 本 tick 从 (0,0) 到 (10,0)。

		var enemy = ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 200f);
		world.GetService<MatchController>().AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 200f); // 本 tick 沿 +Z 从 0 到 2。

		ShooterGame.Step(world);

		var enemyPos = enemy.GetComponent<Position>()!;
		var enemyPlan = enemy.GetComponent<MotionPlan>()!;
		float expectedLength = MathF.Sqrt(10f * 10f + 2f * 2f);
		CheckEqu("运动计划：首个有效 tick 使用玩家计划终点寻向 X", enemyPos.X, 10f / expectedLength * 2f);
		CheckEqu("运动计划：首个有效 tick 使用玩家计划终点寻向 Z", enemyPos.Z, 2f - 2f / expectedLength * 2f);
		CheckEqu("运动计划：敌人实际 X 等于计划终点", enemyPos.X, enemyPlan.EndX);
		CheckEqu("运动计划：敌人实际 Z 等于计划终点", enemyPos.Z, enemyPlan.EndZ);
		Check("运动计划：玩家帧内移动改变寻向后不产生虚假命中", !enemy.IsDestroyed);
		Check("运动计划：虚假命中不计分", world.GetService<MatchController>().Score == 0);
	}

	// ---------- 3c：禁用行为 → 静止计划，不产生幽灵轨迹 ----------

	private static void TestDisabledActionStationary()
	{
		// P1 回归：禁用 EnemyControllerAction 后，敌人应停留在原位（静止计划），
		// 子弹穿过它仍能命中真实当前位置；不因「O1 跳过 OnTick 但计划已发布」而与幽灵轨迹碰撞。
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		var enemy = ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 30f);
		enemy.GetComponent<EnemyControllerAction>()!.Enabled = false; // 禁用行为：不寻敌、不移动。
		world.GetService<MatchController>().AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 100);

		// 首帧后敌人仍应停在 z=2（静止计划）；子弹尚未到达，且敌人未被禁用逻辑误动。
		ShooterGame.Step(world);
		Check("禁用行为：敌人停在 z=2（不产生幽灵轨迹）", enemy.GetComponent<Position>()!.Z == 2f);
		Check("禁用行为：首帧敌人仍存活", !enemy.IsDestroyed);

		for (int i = 0; i < 5 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}


		Check("禁用行为：子弹仍命中静止敌人（真实位置）", CountWith<EnemyFaction>(world) == 0);
		Check("禁用行为：命中计分+1", world.GetService<MatchController>().Score == 1);
	}

	// ---------- 3d：禁用玩家控制器 → 即使有输入也不移动 ----------

	private static void TestDisabledPlayerNoMove()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");
		var player = FindPlayer(world)!;
		player.GetComponent<PlayerControllerAction>()!.Enabled = false; // 禁用控制器。

		world.GetService<InputService>().MoveX = 1;
		ShooterGame.Step(world);
		ShooterGame.Step(world);

		Check("禁用控制器：玩家不移动", player.GetComponent<Position>()!.X == 0f);
	}

	// ---------- 3e：小幅相对扫掠仍应判定（P2 回归）----------

	private static void TestSmallSweepCrossing()
	{
		// P2：旧实现在 lengthSquared<0.0001 时退化为「起点」距离，漏判跨过合并半径的小幅相对运动。
		// 例：合并半径 1，相对距离 1.004→0.996（位移 0.008，平方 6.4e-5），应在某 t 处距离 <1 命中。
		var world = ShooterGame.CreateWorld();
		var resolver = world.GetService<CollisionResolver>();
		// A 静止在原点；B 相对距离 1.004→0.996——若退化为「起点」会漏判，正确应落到 0.996。
		float dist = resolver.SegmentSegmentDistance(
			0f, 0f, 0f, 0f,      // A 静止在原点
			0f, 1.004f, 0f, 0.996f); // B: (0,1.004)→(0,0.996)，朝原点靠近
		Check("小幅扫掠：相对运动到 0.996 处最短距离 < 0.996", dist < 0.997f);
		Check("小幅扫掠：距离 < 合并半径 1（应命中）", dist < 1f);
		Check("小幅扫掠：不等于起点 1.004（未退化为点）", dist < 1.0001f);
	}

	// ---------- 4：非致死伤害不误杀 ----------

	private static void TestNonLethalDamage()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		var match = world.GetService<MatchController>();
		var enemy = ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 0, health: 2);
		match.AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 30, damage: 1);

		for (int i = 0; i < 30 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}

		Check("非致死：目标存活", !enemy.IsDestroyed);
		Check("非致死：未计分", match.Score == 0);
		Check("非致死：血量降至 1", enemy.GetComponent<Health>()!.Current == 1);
	}

	// ---------- 5：四面生成覆盖 ----------

	private static void TestSpawnCoverage()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.Interval = 0;
		config.MaxAlive = 64;
		config.SpawnRadius = 20;

		var seen = new HashSet<GameObject>();
		bool px = false, nx = false, pz = false, nz = false;
		for (int i = 0; i < 66; i++)
		{
			ShooterGame.Step(world);
			foreach (var enemy in ShooterWorld.QueryObjects(world, o => o.GetComponent<EnemyFaction>() != null))
			{
				if (!seen.Add(enemy))
				{
					continue;
				}
				var pos = enemy.GetComponent<Position>()!;
				if (MathF.Abs(pos.X) >= MathF.Abs(pos.Z))
				{
					px |= pos.X >= 0;
					nx |= pos.X < 0;
				}
				else
				{
					pz |= pos.Z >= 0;
					nz |= pos.Z < 0;
				}
			}
		}
		Check($"生成覆盖：{seen.Count} 个敌人（期望 64）", seen.Count == 64);
		Check("生成覆盖：四方向均出现", px && nx && pz && nz);
	}

	// ---------- 6：敌人寻玩家 ----------

	private static void TestEnemySeek()
	{
		var seekWorld = ShooterGame.CreateWorld();
		var seekConfig = seekWorld.GetService<SpawnConfig>();
		seekConfig.MaxAlive = 0;
		ClearObjectsExcept(seekWorld, "Player");
		var enemy = ShooterFactory.SpawnEnemy(seekWorld, 5, 0, moveSpeed: 3.5f);

		float startX = enemy.GetComponent<Position>()!.X;
		for (int i = 0; i < 30; i++)
		{
			ShooterGame.Step(seekWorld);
		}

		Check("寻敌：敌人向玩家靠近（X 减小）", enemy.GetComponent<Position>()!.X < startX - 0.5f);
	}

	// ---------- 7：GameOver 冻结（Paused 全局）----------

private static void TestGameOverFreeze()
	{
		// 保留 Game 宿主（EnemySpawner 启用）——验证 Paused 真正冻结生成，而非宿主被清导致"无生成"。
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.Interval = 0; // 每 tick 都想生成
		config.MaxAlive = 64;

		var match = world.GetService<MatchController>();
		ShooterFactory.SpawnEnemy(world, 0, 0, moveSpeed: 0); // 与玩家重叠 → 接触
		match.AliveEnemies = 1;

		ShooterGame.Step(world);
		Check("GameOver：接触敌人进入 GameOver", match.Phase == GamePhase.GameOver);
		Check("GameOver：世界 Paused（全局冻结）", world.Paused);

		// 记录 GameOver 时敌人数；Paused 后生成器应被冻结（不新增），玩家也冻结。
		int enemyCount = CountWith<EnemyFaction>(world);
		var player = FindPlayer(world)!;
		float frozenX = player.GetComponent<Position>()!.X;
		world.GetService<InputService>().MoveX = 1;
		for (int i = 0; i < 8; i++)
		{
			ShooterGame.Step(world);
		}
		Check("GameOver：玩家冻结", player.GetComponent<Position>()!.X == frozenX);
		Check("GameOver：Paused 冻结生成（敌人数不变）", CountWith<EnemyFaction>(world) == enemyCount);
		Check("GameOver：AliveEnemies 不变", match.AliveEnemies == enemyCount);

		// 从该 Paused 世界 Restart → 恢复 Playing + 解锁 Paused，生成器恢复。
		ShooterGame.Restart(world);
		Check("恢复：Restart 后 Playing", world.GetService<MatchController>().Phase == GamePhase.Playing);
		Check("恢复：Restart 后非 Paused", !world.Paused);
		Check("恢复：重启后玩家存在", FindPlayer(world) != null);
	}

	// ---------- 8：旧句柄（已销毁对象）不再结算 ----------

	private static void TestStaleHandleNoResolution()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		// 组件间直接调用：对已销毁敌人调 ApplyDamage 应被拒绝（Owner.IsDestroyed 短路）。
		// 关键：Destroy 前先保存 Health 引用与 Current（否则 Destroy 后 GetComponent 恒 null，
		// 短路表达式从未真正调用 ApplyDamage）。
		var oldTarget = ShooterFactory.SpawnEnemy(world, 10, 0, moveSpeed: 0);
		var health = oldTarget.GetComponent<Health>()!;
		int currentBefore = health.Current;
		oldTarget.Destroy();
		world.GetService<MatchController>().AliveEnemies = 0;

		Check("旧句柄：销毁后 ApplyDamage 被拒绝", !health.ApplyDamage(1));
		Check("旧句柄：Current 未变", health.Current == currentBefore);
		Check("旧句柄：未计分", world.GetService<MatchController>().Score == 0);

	}

	// ---------- 9：重启 ----------

	private static void TestRestart()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;

		inputFire(world);
		var match = world.GetService<MatchController>();
		ShooterGame.Step(world);
		Check("重启前：有投射物", CountWith<ProjectileTag>(world) == 1);

		match.Score = 5;
		match.AliveEnemies = 2;
		ShooterGame.Restart(world);

		Check("重启后：Score=0", match.Score == 0);
		Check("重启后：AliveEnemies=0", match.AliveEnemies == 0);
		Check("重启后：玩家重新存在", FindPlayer(world) != null);
		Check("重启后：投射物清空", CountWith<ProjectileTag>(world) == 0);
		Check("重启后：Phase=Playing", world.GetService<MatchController>().Phase == GamePhase.Playing);
		Check("重启后：TickIndex=0", world.TickIndex == 0);
	}

	// ---------- 10：确定性（重放同输入 → 同结果）----------

	private static void TestDeterminism()
	{
		RunResult a = RunOnce();
		RunResult b = RunOnce();
		Check($"确定性：hash={a.Hash} vs {b.Hash}", a.Hash == b.Hash);
		Check("确定性：最终状态一致", a.Score == b.Score && a.Alive == b.Alive);

		// Restart 后同输入重放也应一致（世界重置确定性）。
		var world = ShooterGame.CreateWorld();
		ShooterGame.Restart(world);
		var c = ReplayOnce(world);
		Check("确定性：Restart 后重放与首局一致", c.Hash == a.Hash);
	}

	private static RunResult RunOnce()
	{
		var world = ShooterGame.CreateWorld();
		return ReplayOnce(world);
	}

	private static RunResult ReplayOnce(GameWorld world)
	{
		var input = world.GetService<InputService>();
		ulong hash = 1469598103934665603UL;
		// 一段带移动+射击的输入脚本（确定性）；hash 覆盖完整玩法状态（对象序 + 位置 + 阵营 + 阶段 + TickIndex）。
		for (int i = 0; i < 60; i++)
		{
			Mix(ref hash, (int)world.TickIndex);
			input.MoveX = (i % 3 == 0) ? 1 : 0;
			input.FirePressed = (i % 5 == 0);
			ShooterGame.Step(world);
			Mix(ref hash, world.GetService<MatchController>().Score);
			MixWorldState(ref hash, world);
		}
		var match = world.GetService<MatchController>();
		MixWorldState(ref hash, world);
		return new RunResult(hash, match.Score, match.AliveEnemies);
	}

	private static void MixWorldState(ref ulong hash, GameWorld world)
	{
		// 按稳定对象序（创建序 = Roots 深度优先）混入关键组件状态。
		foreach (var obj in ShooterWorld.AllObjects(world))
		{
			Mix(ref hash, obj.Name.GetHashCode());
			var pos = obj.GetComponent<Position>();
			if (pos != null)
			{
				Mix(ref hash, BitConverter.SingleToInt32Bits(pos.X));
				Mix(ref hash, BitConverter.SingleToInt32Bits(pos.Z));
			}
			Mix(ref hash, obj.GetComponent<PlayerFaction>() != null ? 1 : 0);
			Mix(ref hash, obj.GetComponent<EnemyFaction>() != null ? 2 : 0);
			Mix(ref hash, obj.GetComponent<ProjectileTag>() != null ? 4 : 0);
		}
	}

	private static void Mix(ref ulong hash, int value)
	{
		hash ^= (ulong)(uint)value;
		hash *= 1099511628211UL;
	}

	// ---------- 工具 ----------

	private static void inputFire(GameWorld world)
	{
		var input = world.GetService<InputService>();
		input.FirePressed = false;
		ShooterGame.Step(world);
		input.FirePressed = true;
		ShooterGame.Step(world);
		input.FirePressed = false;
	}

	private static void SetPlayerCooldownZero(GameWorld world)
	{
		foreach (var obj in ShooterWorld.QueryObjects(world, o => o.GetComponent<WeaponConfig>() != null))
		{
			obj.GetComponent<WeaponConfig>()!.CooldownSeconds = 0;
		}
	}

	private static GameObject? FindPlayer(GameWorld world)
	{
		foreach (var obj in ShooterWorld.QueryObjects(world, o => o.GetComponent<PlayerFaction>() != null))
		{
			return obj;
		}
		return null;
	}

	private static int CountWith<T>(GameWorld world) where T : GameComponent
	{
		int count = 0;
		foreach (var obj in ShooterWorld.QueryObjects(world, o => o.GetComponent<T>() != null))
		{
			count++;
		}
		return count;
	}

	/// <summary>清空指定名字以外的全部对象（测试隔离用；先快照再销毁防枚举失效）。</summary>
	private static void ClearObjectsExcept(GameWorld world, string keepName)
	{
		foreach (var obj in CollectAll(world))
		{
			if (obj.Name != keepName)
			{
				obj.Destroy();
			}
		}
	}

	private static GameObject[] CollectAll(GameWorld world)
	{
		var list = new List<GameObject>();
		foreach (var obj in ShooterWorld.AllObjects(world))
		{
			list.Add(obj);
		}
		return list.ToArray();
	}

private readonly struct RunResult(ulong hash, int score, int alive)
	{
		public ulong Hash { get; } = hash;
		public int Score { get; } = score;
		public int Alive { get; } = alive;
	}
}
