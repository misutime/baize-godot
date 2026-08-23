// SPDX-License-Identifier: MIT
// ShooterBehaviors.cs —— O2 行为组件（替代 ECS System：组件挂对象，OnTick 自驱）
//
// 每个行为组件 = 原 ECS System 逻辑的组件化。宿主对象：
// - MoveObjectBehavior / PlayerInputBehavior / FireWeaponBehavior：玩家/敌人/投射物行为
// - ProjectileBehavior：投射物生命周期（移动 + 扫掠命中 + 射程清理）
// - EnemyAIBehavior：寻敌 + 接触判定（GameOver 请求）
// - EnemySpawnerBehavior：挂"世界宿主"对象（Game）
//
// GameOver 冻结：所有玩法行为 OnTick 首行检查 IsPlaying（原 ECS RunInState(Playing) 的等价）。
// 硬门禁：Gameplay 零 Node API、零 Friflo/Baize.Ecs；对象经 world.Roots/Children 遍历访问。

using System;
using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>移动：previous=current，current += velocity * delta（原 MoveSystem）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity) })]
public sealed class MoveObjectBehavior : GameComponent
{
	public override void OnTick(float delta)
	{
		// GameOver 冻结（原 ECS RunInState(Playing) 的 Go 版等价）。
		if (!ShooterWorld.IsPlaying(World!))
		{
			return;
		}
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		prev.X = pos.X;
		prev.Z = pos.Z;
		pos.X += vel.X * delta;
		pos.Z += vel.Z * delta;
	}
}

/// <summary>玩家输入 → 速度（原 ApplyPlayerInputSystem）。InputService 由测试/回放注入。</summary>
[GameComponent(Requires = new[] { typeof(Velocity), typeof(MoveSpeed), typeof(PlayerInputMarker) })]
public sealed class PlayerInputBehavior : GameComponent
{
	private InputService? _input;

	public override void OnStart()
	{
		_input = World!.GetService<InputService>();
	}

	public override void OnTick(float delta)
	{
		if (!ShooterWorld.IsPlaying(World!))
		{
			return;
		}
		var vel = Owner!.GetComponent<Velocity>()!;
		var speed = Owner!.GetComponent<MoveSpeed>()!;
		vel.X = _input!.MoveX * speed.Value;
		vel.Z = _input!.MoveZ * speed.Value;
	}
}

/// <summary>射击：冷却计时 + FirePressed 边沿 → 生成投射物（原 FireWeaponSystem）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(WeaponConfig), typeof(Cooldown), typeof(PlayerInputMarker) })]
public sealed class FireWeaponBehavior : GameComponent
{
	private InputService? _input;
	private GameWorld? _world;

	public override void OnStart()
	{
		_world = World;
		_input = _world!.GetService<InputService>();
	}

	public override void OnTick(float delta)
	{
		if (!ShooterWorld.IsPlaying(World!))
		{
			return;
		}
		var pos = Owner!.GetComponent<Position>()!;
		var weapon = Owner!.GetComponent<WeaponConfig>()!;
		var cooldown = Owner!.GetComponent<Cooldown>()!;

		cooldown.Remaining -= delta;
		if (!_input!.ConsumeFireEdge() || cooldown.Remaining > 0)
		{
			return;
		}

		cooldown.Remaining = weapon.CooldownSeconds;
		// 固定朝 +Z 发射（与 ECS 参考版一致）。
		ShooterFactory.SpawnProjectile(_world!, pos.X, pos.Z, 0, weapon.ProjectileSpeed);
	}
}

/// <summary>投射物生命周期：移动 + 扫掠命中敌人（Previous→Current 线段 vs 敌人圆）+ 射程清理。
/// 命中即对目标结算伤害（伤害/死亡/计分由 MatchState 去重）。原 SweptProjectileHitSystem + CleanupProjectilesSystem。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity), typeof(ProjectileConfig), typeof(TravelDistance), typeof(CollisionRadius), typeof(ProjectileTag) })]
public sealed class ProjectileBehavior : GameComponent
{
	private GameWorld? _world;

	public override void OnStart()
	{
		_world = World;
	}

public override void OnTick(float delta)
	{
		if (!ShooterWorld.IsPlaying(World!))
		{
			return;
		}
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		var config = Owner!.GetComponent<ProjectileConfig>()!;
		var travelled = Owner!.GetComponent<TravelDistance>()!;
		var radius = Owner!.GetComponent<CollisionRadius>()!;

		// 移动（先记录 previous）。
		prev.X = pos.X;
		prev.Z = pos.Z;
		pos.X += vel.X * delta;
		pos.Z += vel.Z * delta;

		// 扫掠命中：对每个存活敌人做线段-圆距离。hit 即结算并销毁**本投射物**；
		// 目标销毁由 MatchState 延迟到本 Tick 末（同 Tick 多弹都能命中同一目标，reviewer P1）。
		foreach (var enemy in ShooterWorld.QueryObjects(_world!, o => o.GetComponent<EnemyFaction>() != null))
		{
			var enemyPos = enemy.GetComponent<Position>();
			var enemyRadius = enemy.GetComponent<CollisionRadius>();
			if (enemyPos == null || enemyRadius == null || enemy.IsDestroyed)
			{
				continue;
			}
			float combined = radius.Value + enemyRadius.Value;
			float distance = SegmentPointDistance(
				prev.X, prev.Z, pos.X, pos.Z, enemyPos.X, enemyPos.Z);
			if (distance > combined)
			{
				continue;
			}
// 命中：登记伤害（目标延迟销毁 + 源删除，由 FlushFrame 帧末仅 Playing 提交）。本帧退出，不立即销毁。
			ShooterWorld.ResolveHit(_world!, Owner!, enemy, config.Damage);
			return;
		}
// 射程累计 + 越界删除：登记到帧末（reviewer P1 第五/六轮：TravelDistance 与 Cleanup 一起仅 Playing 提交）。
		float frameTravel = MathF.Sqrt(vel.X * vel.X + vel.Z * vel.Z) * delta;
		ShooterWorld.ScheduleProjectileUpdate(_world!, Owner!, frameTravel);
	}

	private static float SegmentPointDistance(
		float x1, float z1, float x2, float z2, float pointX, float pointZ)
	{
		float dx = x2 - x1;
		float dz = z2 - z1;
		float lengthSquared = dx * dx + dz * dz;
		if (lengthSquared < 0.0001f)
		{
			float pointDx = pointX - x1;
			float pointDz = pointZ - z1;
			return MathF.Sqrt(pointDx * pointDx + pointDz * pointDz);
		}
		float projection = ((pointX - x1) * dx + (pointZ - z1) * dz) / lengthSquared;
		projection = MathF.Max(0, MathF.Min(1, projection));
		float closestX = x1 + projection * dx;
		float closestZ = z1 + projection * dz;
		float closestDx = pointX - closestX;
		float closestDz = pointZ - closestZ;
		return MathF.Sqrt(closestDx * closestDx + closestDz * closestDz);
	}
}

/// <summary>敌人 AI：一次完成「寻路→移动→接触」——保证接触判定基于**移动后**的位置（reviewer P1：参考版 Collision 在 Simulation 之后）。
/// Requires 含 SeekTargetMarker（能力标记，reviewer P2）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(Velocity), typeof(MoveSpeed), typeof(CollisionRadius), typeof(EnemyFaction), typeof(SeekTargetMarker) })]
public sealed class EnemyAIBehavior : GameComponent
{
	private GameWorld? _world;

	public override void OnStart()
	{
		_world = World;
	}

	public override void OnTick(float delta)
	{
		if (!ShooterWorld.IsPlaying(World!))
		{
			return;
		}
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		var speed = Owner!.GetComponent<MoveSpeed>()!;
		var radius = Owner!.GetComponent<CollisionRadius>()!;

		// 1) 寻路：目标方向速度。
		GameObject? player = null;
		foreach (var candidate in ShooterWorld.QueryObjects(_world!, o => o.GetComponent<PlayerFaction>() != null))
		{
			player = candidate;
			break;
		}
		if (player == null)
		{
			vel.X = 0;
			vel.Z = 0;
			return;
		}
		var playerPos = player.GetComponent<Position>()!;
		var playerRadius = player.GetComponent<CollisionRadius>()!;

		float dx = playerPos.X - pos.X;
		float dz = playerPos.Z - pos.Z;
		float length = MathF.Sqrt(dx * dx + dz * dz);
		if (length > 0.01f)
		{
			vel.X = dx / length * speed.Value;
			vel.Z = dz / length * speed.Value;
		}
		else
		{
			vel.X = 0;
			vel.Z = 0;
		}

		// 2) 移动（使用上一帧速度——参考版 MoveSystem 消费 SeekPlayerSystem 写入的速度）。
		prev.X = pos.X;
		prev.Z = pos.Z;
		pos.X += vel.X * delta;
		pos.Z += vel.Z * delta;

		// 3) 接触判定（移动后位置）。
		float newDx = playerPos.X - pos.X;
		float newDz = playerPos.Z - pos.Z;
		float newDist = MathF.Sqrt(newDx * newDx + newDz * newDz);
		if (newDist <= playerRadius.Value + radius.Value)
		{
			ShooterWorld.RequestGameOver(_world!);
		}
	}
}

/// <summary>敌人生成器（挂世界宿主）：固定节拍 + TickIndex 确定性 HashTick（原 SpawnEnemiesSystem）。</summary>
[GameComponent]
public sealed class EnemySpawnerBehavior : GameComponent
{
	private GameWorld? _world;
	private MatchState? _match;
	private SpawnConfig? _config;
	private SpawnState? _state;

	public override void OnStart()
	{
		_world = World;
		_match = _world!.GetService<MatchState>();
		_config = _world!.GetService<SpawnConfig>();
		_state = _world!.GetService<SpawnState>();
	}

	public override void OnTick(float delta)
	{
		if (_match!.Phase != GamePhase.Playing)
		{
			return;
		}
		_state!.Remaining -= delta;
		if (_state.Remaining > 0)
		{
			return;
		}
		_state.Remaining = _config!.Interval;
		if (_match.AliveEnemies >= _config.MaxAlive)
		{
			return;
		}

		ulong random = DeterministicRandom.HashTick(World!.TickIndex);
		float edgeOffset = (((random >> 8) & 0x00FF_FFFFUL) / 16_777_215.0f * 2.0f - 1.0f)
			* _config.SpawnRadius;
		(float x, float z) = (random & 3UL) switch
		{
			0 => (_config.SpawnRadius, edgeOffset),
			1 => (-_config.SpawnRadius, edgeOffset),
			2 => (edgeOffset, _config.SpawnRadius),
			_ => (edgeOffset, -_config.SpawnRadius),
		};
		ShooterFactory.SpawnEnemy(_world!, x, z);
		_match.AliveEnemies++;
	}
}
