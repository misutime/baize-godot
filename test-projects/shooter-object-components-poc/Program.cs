// SPDX-License-Identifier: MIT
// Program.cs —— O2 Go 版 Shooter 验收：8 场景断言 + 玩法闭环（对应 ECS shooter-poc 语义）

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
		TestSameTickDuplicateHit();
		TestEnemySeekAndSpawnCoverage();
		TestGameOverFreeze();
		TestStaleHandleNoResolution();
		TestRestart();
		TestReview_越界命中优先();
		TestReview_跨入接触半径当帧GameOver();
		TestReview_GameOver同帧回滚即时创建();
		TestReview_GameOver整帧命中丢弃();
		TestReview_非致死伤害不误杀();
		TestReview_GameOver同帧丢弃射程清理();
		TestReview_Playing越界正常清理();

		Console.WriteLine($"shooter-object-components-poc: failures={_failures}");
		if (_failures != 0)
		{
			return 1;
		}
		Console.WriteLine("O2 验收通过：玩家/敌人/投射物均为 GameObject；玩法闭环不调用 Node API；GDScript/ECS 未参与。");
		return 0;
	}

	// ---------- 场景 1：玩家输入驱动移动 ----------

	private static void TestPlayerMovement()
	{
		var world = ShooterGame.CreateWorld();
		var input = world.GetService<InputService>();

		ShooterGame.Step(world);
		var player = FindPlayer(world)!;
		Check("移动：初始位置 (0,0)", player.GetComponent<Position>()!.X == 0 && player.GetComponent<Position>()!.Z == 0);

		// 输入 +X 一帧：速度 = 8，delta=0.01 → 移动 0.08。
		input.MoveX = 1;
		ShooterGame.Step(world);
		CheckEqu("移动：+X 移动 0.08", player.GetComponent<Position>()!.X, 0.08f);
		Check("移动：Z 不变", player.GetComponent<Position>()!.Z == 0);

		// 停止输入：速度归零，位置冻结。
		input.MoveX = 0;
		ShooterGame.Step(world);
		CheckEqu("移动：停输入后位置不变", player.GetComponent<Position>()!.X, 0.08f);
	}

	// ---------- 场景 2：Fire 边沿只发一弹 ----------

	private static void TestFireEdge()
	{
		var world = ShooterGame.CreateWorld();
		var input = world.GetService<InputService>();
		world.GetService<SpawnConfig>().MaxAlive = 0; // 关闭生成，只测射击
		SetPlayerCooldownZero(world); // 冷却 0，允许 5 帧内连续两弹
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

	// ---------- 场景 3：投射物扫掠命中敌人并死亡/计分 ----------

	private static void TestProjectileHitsEnemy()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 0);
		world.GetService<MatchState>().AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 30); // 朝 +Z

		// 弹以 30/s 朝 +Z 飞；敌人静置 (0,2)。飞至半径范围（敌人 0.5 + 弹 0.2 = 0.7）即扫掠命中。
		for (int i = 0; i < 20 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}

		var match = world.GetService<MatchState>();
		Check("命中：敌人被消灭", CountWith<EnemyFaction>(world) == 0);
		Check("命中：计分+1", match.Score == 1);
		Check("命中：投射物消失", CountWith<ProjectileTag>(world) == 0);
		Check("命中：AliveEnemies 归零", match.AliveEnemies == 0);
	}

	// ---------- 场景 4：同 Tick 多弹命中同目标只结算一次 ----------

	private static void TestSameTickDuplicateHit()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 0);
		world.GetService<MatchState>().AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, -0.05f, 0, 0, 30);
		ShooterFactory.SpawnProjectile(world, 0.05f, 0, 0, 30);

		for (int i = 0; i < 20 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}

		var match = world.GetService<MatchState>();
		Check("去重：同目标多次命中只得 1 分", match.Score == 1);
		Check("去重：敌人已消灭", CountWith<EnemyFaction>(world) == 0);
		Check("去重：AliveEnemies 归零", match.AliveEnemies == 0);
		// reviewer P1：同 Tick 两弹命中同一目标，两弹都被消费（延迟目标销毁保证第二弹也能命中并销毁）。
		Check("去重：两弹均被消费（投射物清零）", CountWith<ProjectileTag>(world) == 0);
	}

	// ---------- 场景 5：敌人寻玩家 + 四面生成覆盖 ----------

	private static void TestEnemySeekAndSpawnCoverage()
	{
		// 生成覆盖：spawnInterval=0 → 每 Tick 生成直到 MaxAlive；断言四方向覆盖。
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
			foreach (var spawnedEnemy in ShooterWorld.QueryObjects(world, o => o.GetComponent<EnemyFaction>() != null))
			{
				if (!seen.Add(spawnedEnemy))
				{
					continue;
				}
				var pos = spawnedEnemy.GetComponent<Position>()!;
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

		// 寻玩家：静止放置敌人远离玩家，跑几帧后位置应朝玩家位移。
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

	// ---------- 场景 6：GameOver 冻结 ----------

	private static void TestGameOverFreeze()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.MaxAlive = 10;
		config.Interval = 0;
		ClearObjectsExcept(world, "Player");
		ShooterFactory.SpawnEnemy(world, 0, 0, moveSpeed: 0); // 与玩家重叠 → 接触
		world.GetService<MatchState>().AliveEnemies = 1;

		ShooterGame.Step(world);
		var match = world.GetService<MatchState>();
		Check("GameOver：接触敌人进入 GameOver", match.Phase == GamePhase.GameOver);

		var player = FindPlayer(world)!;
		float frozenX = player.GetComponent<Position>()!.X;
		int enemyCount = CountWith<EnemyFaction>(world);
		world.GetService<InputService>().MoveX = 1; // 即使有输入也不应移动
		for (int i = 0; i < 8; i++)
		{
			ShooterGame.Step(world);
		}
		Check("GameOver：玩家冻结", player.GetComponent<Position>()!.X == frozenX);
		Check("GameOver：不再生成/删除敌人", CountWith<EnemyFaction>(world) == enemyCount);
		Check("GameOver：AliveEnemies 不变", match.AliveEnemies == enemyCount);
	}

	// ---------- 场景 7：旧句柄（已销毁对象）不再结算 ----------

	private static void TestStaleHandleNoResolution()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		var source = ShooterFactory.SpawnProjectile(world, -10, 0, 0, 0);
		var oldTarget = ShooterFactory.SpawnEnemy(world, 10, 0, moveSpeed: 0);
		oldTarget.Destroy(); // 删除旧敌人（句柄变 stale）
		world.GetService<MatchState>().AliveEnemies = 0;

		// 用旧句柄再结算：应因 IsDestroyed 被拒绝（不误计分、不误删）。
		bool resolved = ShooterWorld.ResolveHit(world, source, oldTarget, 1);
		var match = world.GetService<MatchState>();
		Check("旧句柄：结算被拒绝", !resolved);
		Check("旧句柄：未计分", match.Score == 0);
		Check("旧句柄：source 仍存活", !source.IsDestroyed);
	}

	// ---------- 场景 8：重启 ----------

	private static void TestRestart()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;

		// 先制造状态：射击一发（有投射物）。
		inputFire(world);
		var match = world.GetService<MatchState>();
		ShooterGame.Step(world);
		Check("重启前：有投射物", CountWith<ProjectileTag>(world) == 1);

		// 设个分再重启。
		match.Score = 5;
		match.AliveEnemies = 2;
		ShooterGame.Restart(world);

		Check("重启后：Score=0", match.Score == 0);
		Check("重启后：AliveEnemies=0", match.AliveEnemies == 0);
		Check("重启后：玩家重新存在", FindPlayer(world) != null);
		Check("重启后：投射物清空", CountWith<ProjectileTag>(world) == 0);
		Check("重启后：Phase=Playing", world.GetService<MatchState>().Phase == GamePhase.Playing);
		// reviewer P1：重启必须归零 TickIndex（重放同一生成序列）。
		Check("重启后：TickIndex=0", world.TickIndex == 0);
	}

	// reviewer P1：同帧越界（超射程）+ 穿透敌人，仍应先命中。
	private static void TestReview_越界命中优先()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		// 敌人放 (0, 3)，弹速 300、MaxRange=2（一帧 0.01 → 移动 3，越界 + 穿透）。
		ShooterFactory.SpawnEnemy(world, 0, 3, moveSpeed: 0);
		world.GetService<MatchState>().AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 300, maxRange: 2);

		// 第一帧：移动 3（越 2 射程），但扫掠线段(0,0)→(0,3) 穿过 (0,3) 敌人 → 应先命中而非射程清理。
		ShooterGame.Step(world);
		Check("越界命中：敌人被消灭", CountWith<EnemyFaction>(world) == 0);
		Check("越界命中：计分+1", world.GetService<MatchState>().Score == 1);
		Check("越界命中：投射物已消费", CountWith<ProjectileTag>(world) == 0);
	}

	// reviewer P1：接触判定发生在移动之后（敌人在本帧跨入接触半径即当帧 GameOver）。
	private static void TestReview_跨入接触半径当帧GameOver()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		// 敌人距玩家略大于接触半径（0.5+0.5=1.0），本帧向内移动使其 < 1.0。
		// 玩家在 (0,0)，敌人 (0, 1.1)，速度 200 → 一帧 0.01 移动 2 → 越过并 <1.0。
		ShooterFactory.SpawnEnemy(world, 0, 1.1f, moveSpeed: 200f);
		world.GetService<MatchState>().AliveEnemies = 1;

		ShooterGame.Step(world);
		Check("跨入接触：当帧 GameOver", world.GetService<MatchState>().Phase == GamePhase.GameOver);
	}

	// reviewer P1：GameOver 同帧回滚已即时创建的敌人/投射物（参考版 CommandBuffer.Reset 语义）。
// reviewer P1：GameOver 同帧回滚已即时创建的敌人（参考版 CommandBuffer.Reset 语义）。
	private static void TestReview_GameOver同帧回滚即时创建()
	{
		// 保留宿主（生成器 interval=0 每帧生成）+ 玩家。
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.Interval = 0;
		config.MaxAlive = 64;
		// 清掉一个"旧"接触敌人之前的对象；保留 Player 与 Game 宿主。
		ClearObjectsExceptAny(world, new[] { "Player", "Game" });

		var match = world.GetService<MatchState>();
		// 制造一个接触敌人：与玩家重叠 → 本帧接触触发 GameOver。
		ShooterFactory.SpawnEnemy(world, 0, 0, moveSpeed: 0);
		match.AliveEnemies = 1;

		// 本帧：生成器（interval=0）会即时创建一个敌人；随后接触敌人触发 GameOver → 该即时创建应被回滚。
		ShooterGame.Step(world);

		Check("回滚：GameOver 已触发", match.Phase == GamePhase.GameOver);
		Check("回滚：本帧即时创建的敌人被撤销（只剩接触敌人）", CountWith<EnemyFaction>(world) == 1);
		Check("回滚：AliveEnemies 只剩接触敌人", match.AliveEnemies == 1);
	}

	// reviewer P1（第三轮）：GameOver 帧先命中后接触——整帧待提交命中（伤害/计分/目标销毁）应被丢弃。
	private static void TestReview_GameOver整帧命中丢弃()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.MaxAlive = 0;
		ClearObjectsExceptAny(world, new[] { "Player", "Game" });

		var match = world.GetService<MatchState>();
		// 一个敌人在投射物路径上（被打），另一个敌人接触玩家（触发 GameOver，同帧）。
		var source = ShooterFactory.SpawnProjectile(world, 0, 0, 0, 300); // 朝 +Z 高速弹
		var hitTarget = ShooterFactory.SpawnEnemy(world, 0, 0.3f, moveSpeed: 0); // 同帧被扫过
		ShooterFactory.SpawnEnemy(world, 100, 100, moveSpeed: 0); // 静态（不接触）
		world.GetService<MatchState>().AliveEnemies = 2;
		_ = hitTarget;

		// 制造接触：在玩家位置放一个接触敌人（与玩家重叠）。
		ShooterFactory.SpawnEnemy(world, 0, 0, moveSpeed: 0);
		world.GetService<MatchState>().AliveEnemies = 3;

		ShooterGame.Step(world);

		// GameOver 已触发 → 本帧命中（hitTarget 伤害/计分/销毁）整帧丢弃。
		Check("整帧丢弃：GameOver 已触发", match.Phase == GamePhase.GameOver);
		Check("整帧丢弃：未计分（命中被丢）", match.Score == 0);
		Check("整帧丢弃：命中目标未销毁（延迟帧被丢）", !hitTarget.IsDestroyed);
		// reviewer P1 第四轮：GameOver 帧命中源（投射物）应保留（未删除）。
		Check("整帧丢弃：命中源仍存活", !source.IsDestroyed);
	}

	// reviewer P1 第四轮：非致死伤害目标存活并不计分（health=2 受 1 点伤害）。
	private static void TestReview_非致死伤害不误杀()
	{
		var world = ShooterGame.CreateWorld();
		world.GetService<SpawnConfig>().MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		var enemy = ShooterFactory.SpawnEnemy(world, 0, 2, moveSpeed: 0, health: 2);
		world.GetService<MatchState>().AliveEnemies = 1;
		ShooterFactory.SpawnProjectile(world, 0, 0, 0, 30, damage: 1);

		for (int i = 0; i < 30 && CountWith<EnemyFaction>(world) > 0; i++)
		{
			ShooterGame.Step(world);
		}

		var match = world.GetService<MatchState>();
		Check("非致死：目标存活", !enemy.IsDestroyed);
		Check("非致死：未计分", match.Score == 0);
		Check("非致死：AliveEnemies 保持 1", match.AliveEnemies == 1);
		Check("非致死：目标血量降至 1", enemy.GetComponent<Health>()!.Current == 1);
	}

	// reviewer P1（第五轮）：GameOver 同帧丢弃投射物射程清理——投射物先越界、敌人接触同帧 → 投射物保留。
	private static void TestReview_GameOver同帧丢弃射程清理()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.MaxAlive = 0;
		ClearObjectsExceptAny(world, new[] { "Player", "Game" });

// 投射物放 x=10（不与 (0,0) 接触敌人相交，避免扫掠命中），一帧即越界；接触敌人同帧触发 GameOver。
		var projectile = ShooterFactory.SpawnProjectile(world, 10, 0, 0, 300, maxRange: 0.01f);
		ShooterFactory.SpawnEnemy(world, 0, 0, moveSpeed: 0); // 接触触发 GameOver
		world.GetService<MatchState>().AliveEnemies = 1;
		_ = projectile.GetComponent<TravelDistance>();

		ShooterGame.Step(world);
		var match = world.GetService<MatchState>();
		Check("射程清理丢弃：GameOver 已触发", match.Phase == GamePhase.GameOver);
		// GameOver 帧：投射物不应被 Cleanup 删除（仍存活），且 TravelDistance 未累计。
		Check("射程清理丢弃：投射物仍存活", !projectile.IsDestroyed);
		Check("射程清理丢弃：TravelDistance 未累计", projectile.GetComponent<TravelDistance>()!.Value == 0);
	}

	// Playing 帧越界清理仍工作（帧末提交在 Playing 正常累计距离并删除）。
	private static void TestReview_Playing越界正常清理()
	{
		var world = ShooterGame.CreateWorld();
		var config = world.GetService<SpawnConfig>();
		config.MaxAlive = 0;
		ClearObjectsExcept(world, "Player");

		// 投射物朝 +Z 飞，无敌人；一帧越界（maxRange 很小）。
		var projectile = ShooterFactory.SpawnProjectile(world, 0, 0, 0, 300, maxRange: 0.01f);
		var travelled = projectile.GetComponent<TravelDistance>()!;
		ShooterGame.Step(world);

		Check("Playing 越界：投射物被清理（越界删除）", projectile.IsDestroyed);
		Check("Playing 越界：TravelDistance 已累计", travelled.Value > 0);
	}
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

	/// <summary>清空多个名字以外的全部对象。</summary>
	private static void ClearObjectsExceptAny(GameWorld world, string[] keepNames)
	{
		foreach (var obj in CollectAll(world))
		{
			bool keep = false;
			foreach (var n in keepNames)
			{
				if (obj.Name == n)
				{
					keep = true;
					break;
				}
			}
			if (!keep)
			{
				obj.Destroy();
			}
		}
	}

	/// <summary>收集全部对象为数组（销毁安全）。</summary>
	private static GameObject[] CollectAll(GameWorld world)
	{
		var list = new List<GameObject>();
		foreach (var obj in ShooterWorld.AllObjects(world))
		{
			list.Add(obj);
		}
		return list.ToArray();
	}
}
