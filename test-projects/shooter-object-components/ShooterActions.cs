// SPDX-License-Identifier: MIT
// ShooterActions.cs —— O2 行为组件（控制器先规划，OnTick 消费；组件只操作 Owner 能力）
//
// 干净 GameObject-first 模型：
// - 组件直接读/改自己的 Owner 组件，组件间直接调用（如 bullet → enemy.Health.ApplyDamage）。
// - 阶段控制用 GameWorld.Paused（GameOver 时由 MatchController 设 Paused=true → O1 全局冻结），
//   组件不再逐帧检查 IsPlaying。
// - 无 ECS CommandBuffer：创建/命中/死亡都即时、直接（同步销毁 + 对象创建序 tick 天然去重）。

using System;
using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>移动：把 PreviousPosition/Position 精确提交为本 tick MotionPlan 的起点/终点。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(MotionPlan) })]
public sealed class MoveAction : GameComponent
{
	public override void OnTick(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var plan = Owner!.GetComponent<MotionPlan>()!;
		prev.X = plan.StartX;
		prev.Z = plan.StartZ;
		pos.X = plan.EndX;
		pos.Z = plan.EndZ;
	}
}

/// <summary>玩家输入控制器：在 tick 前提交本帧唯一运动计划。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(Velocity), typeof(MotionPlan), typeof(MoveSpeed), typeof(PlayerInputMarker) })]
public sealed class PlayerControllerAction : GameComponent
{
	private InputService? _input;

	public override void OnCreate()
	{
		_input = World!.GetService<InputService>();
	}

	public void PlanMotion(float delta, ulong tickIndex)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		var speed = Owner!.GetComponent<MoveSpeed>()!;
		var plan = Owner!.GetComponent<MotionPlan>()!;
		// 未启用（组件/父链禁用、已销毁）：只提交静止计划——不因 MoveAction 仍启用而误动。
		if (!ShooterWorldHelper.CanTick(this))
		{
			vel.X = 0;
			vel.Z = 0;
			plan.Set(tickIndex, pos.X, pos.Z, pos.X, pos.Z);
			return;
		}
		vel.X = _input!.MoveX * speed.Value;
		vel.Z = _input.MoveZ * speed.Value;
		plan.Set(tickIndex, pos.X, pos.Z, pos.X + vel.X * delta, pos.Z + vel.Z * delta);
	}

}

/// <summary>射击：冷却计时 + Fire 边沿 → 生成一颗子弹对象（直接调 ShooterFactory，非命令缓冲）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(WeaponConfig), typeof(Cooldown), typeof(PlayerInputMarker) })]
public sealed class WeaponAction : GameComponent
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
		var pos = Owner!.GetComponent<Position>()!;
		var weapon = Owner!.GetComponent<WeaponConfig>()!;
		var cooldown = Owner!.GetComponent<Cooldown>()!;

		cooldown.Remaining -= delta;
		if (!_input!.ConsumeFireEdge() || cooldown.Remaining > 0)
		{
			return;
		}
		cooldown.Remaining = weapon.CooldownSeconds;
		ShooterFactory.SpawnProjectile(_world!, pos.X, pos.Z, 0, weapon.ProjectileSpeed);
	}
}

/// <summary>子弹生命周期：移动 + 扫掠命中敌人（线段-圆距离）。命中 → enemy.Health.ApplyDamage（直接调用），
/// 若致死则 owner 已销毁、本弹也销毁；越界销毁（即时，非帧末）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity), typeof(MotionPlan), typeof(ProjectileConfig), typeof(TravelDistance), typeof(CollisionRadius), typeof(ProjectileTag) })]
public sealed class BulletAction : GameComponent
{
	private GameWorld? _world;
	private MatchController? _match;
	private CollisionResolver? _resolver;

	public override void OnStart()
	{
		_world = World;
		_match = _world!.GetService<MatchController>();
		_resolver = _world!.GetService<CollisionResolver>();
	}

	public void PlanMotion(float delta, ulong tickIndex)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		var plan = Owner!.GetComponent<MotionPlan>()!;
		// 未启用：静止计划（不移动；若仍作为可碰撞对象存在则保留在当前位置，避免幽灵轨迹）。
		if (!ShooterWorldHelper.CanTick(this))
		{
			plan.Set(tickIndex, pos.X, pos.Z, pos.X, pos.Z);
			return;
		}
		plan.Set(tickIndex, pos.X, pos.Z, pos.X + vel.X * delta, pos.Z + vel.Z * delta);
	}


	public override void OnTick(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		var config = Owner!.GetComponent<ProjectileConfig>()!;
		var travelled = Owner!.GetComponent<TravelDistance>()!;
		var radius = Owner!.GetComponent<CollisionRadius>()!;

		var plan = Owner!.GetComponent<MotionPlan>()!;

		// 移动与碰撞共同消费控制器在 tick 前提交的同一计划。
		prev.X = plan.StartX;
		prev.Z = plan.StartZ;
		pos.X = plan.EndX;
		pos.Z = plan.EndZ;

		// 命中只读双方冻结的本帧计划；不会观察到对方执行到一半的实时位置。
		var self = plan;
		foreach (var enemy in ShooterWorldHelper.QueryObjects(_world!, o => o.GetComponent<EnemyFaction>() != null))
		{
			if (enemy.IsDestroyed)
			{
				continue;
			}
			var enemyRadius = enemy.GetComponent<CollisionRadius>();
			if (enemyRadius == null)
			{
				continue;
			}
			var enemyPlan = enemy.GetComponent<MotionPlan>();
			if (enemyPlan == null || enemyPlan.TickIndex != _world!.TickIndex)
			{
				continue; // 本 tick 内新建的对象按 O1 快照语义从下一 tick 才参与。
			}
			float combined = radius.Value + enemyRadius.Value;
			float distance = _resolver!.SegmentSegmentDistance(
				self.StartX, self.StartZ, self.EndX, self.EndZ,
				enemyPlan.StartX, enemyPlan.StartZ, enemyPlan.EndX, enemyPlan.EndZ);
			if (distance > combined)
			{
				continue;
			}


			// 命中：敌人 Health.ApplyDamage；致死则敌人自动销毁并计分（MatchController.OnEnemyKilled）。
			var health = enemy.GetComponent<Health>();
			if (health != null && health.ApplyDamage(config.Damage))
			{
				_match!.OnEnemyKilled();
			}
			Owner!.Destroy();
			return;
		}


		// 射程清理（即时；受 world 冻结控制——GameOver 时 Paused 停 tick 不执行）。
		float moveX = plan.EndX - plan.StartX;
		float moveZ = plan.EndZ - plan.StartZ;
		travelled.Value += MathF.Sqrt(moveX * moveX + moveZ * moveZ);
		if (travelled.Value > config.MaxRange)
		{
			Owner!.Destroy();
		}
	}
}

/// <summary>敌人控制器：tick 前唯一生成寻敌运动计划；OnTick 只消费该计划并做接触判定。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity), typeof(MotionPlan), typeof(MoveSpeed), typeof(CollisionRadius), typeof(EnemyFaction), typeof(SeekTargetMarker) })]
public sealed class EnemyControllerAction : GameComponent
{
	private GameWorld? _world;
	private MatchController? _match;
	private GameObject? _plannedPlayer;

	public override void OnCreate()
	{
		_world = World;
		_match = _world!.GetService<MatchController>();
	}

	/// <summary>基于玩家本帧计划终点生成敌人的唯一运动计划，并同步提交本帧速度。</summary>
	public void PlanMotion(float delta, ulong tickIndex)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var vel = Owner.GetComponent<Velocity>()!;
		var plan = Owner.GetComponent<MotionPlan>()!;
		// 未启用（组件/父链禁用、已销毁）：只提交静止计划——保持可碰撞但不再移动，
		// 子弹命中仍按它真实当前位置判定，避免与「O1 跳过 OnTick 的幽灵轨迹」碰撞。
		if (!ShooterWorldHelper.CanTick(this))
		{
			vel.X = 0;
			vel.Z = 0;
			_plannedPlayer = null;
			plan.Set(tickIndex, pos.X, pos.Z, pos.X, pos.Z);
			return;
		}
		_plannedPlayer = FindPlayer();
		if (_plannedPlayer == null)
		{
			vel.X = 0;
			vel.Z = 0;
			plan.Set(tickIndex, pos.X, pos.Z, pos.X, pos.Z);
			return;
		}
		var playerPos = _plannedPlayer.GetComponent<Position>()!;
		var playerPlan = _plannedPlayer.GetComponent<MotionPlan>();
		float targetX = playerPlan != null && playerPlan.TickIndex == tickIndex ? playerPlan.EndX : playerPos.X;
		float targetZ = playerPlan != null && playerPlan.TickIndex == tickIndex ? playerPlan.EndZ : playerPos.Z;
		float dx = targetX - pos.X;
		float dz = targetZ - pos.Z;
		float length = MathF.Sqrt(dx * dx + dz * dz);
		var speed = Owner.GetComponent<MoveSpeed>()!;
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
		plan.Set(tickIndex, pos.X, pos.Z, pos.X + vel.X * delta, pos.Z + vel.Z * delta);
	}


	public override void OnTick(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner.GetComponent<PreviousPosition>()!;
		var plan = Owner.GetComponent<MotionPlan>()!;
		var radius = Owner.GetComponent<CollisionRadius>()!;

		prev.X = plan.StartX;
		prev.Z = plan.StartZ;
		pos.X = plan.EndX;
		pos.Z = plan.EndZ;

		if (_plannedPlayer == null || _plannedPlayer.IsDestroyed)
		{
			return;
		}
		var playerRadius = _plannedPlayer.GetComponent<CollisionRadius>()!;
		var playerPlan = _plannedPlayer.GetComponent<MotionPlan>();
		float playerX = playerPlan != null && playerPlan.TickIndex == _world!.TickIndex
			? playerPlan.EndX : _plannedPlayer.GetComponent<Position>()!.X;
		float playerZ = playerPlan != null && playerPlan.TickIndex == _world!.TickIndex
			? playerPlan.EndZ : _plannedPlayer.GetComponent<Position>()!.Z;
		float newDx = playerX - pos.X;
		float newDz = playerZ - pos.Z;
		float newDist = MathF.Sqrt(newDx * newDx + newDz * newDz);
		if (newDist <= playerRadius.Value + radius.Value)
		{
			_match!.RequestGameOver();
		}
	}

	private GameObject? FindPlayer()
	{
		foreach (var candidate in ShooterWorldHelper.QueryObjects(_world!, o => o.GetComponent<PlayerFaction>() != null))
		{
			return candidate;
		}
		return null;
	}
}


/// <summary>敌人生成器（挂世界宿主 "Game"）：固定节拍 + TickIndex 确定性 HashTick。</summary>
[GameComponent]
public sealed class EnemySpawnerAction : GameComponent
{
	private GameWorld? _world;
	private MatchController? _match;
	private SpawnConfig? _config;
	private SpawnState? _state;

	public override void OnStart()
	{
		_world = World;
		_match = _world!.GetService<MatchController>();
		_config = _world!.GetService<SpawnConfig>();
		_state = _world!.GetService<SpawnState>();
	}

	public override void OnTick(float delta)
	{
		_state!.Remaining -= delta;
		if (_state.Remaining > 0)
		{
			return;
		}
		_state.Remaining = _config!.Interval;
		if (_match!.AliveEnemies >= _config.MaxAlive)
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
		_match.OnEnemySpawned();
	}
}

/// <summary>碰撞几何：顺序无关的扫掠距离（共享，避免重复代码；作为 Service 注入保持组件自包含）。</summary>
public sealed class CollisionResolver
{
	/// <summary>两条运动轨迹 (a1→a2) 与 (b1→b2) 的同步扫掠最短距离。
	/// 各点沿线段匀速运动（同为 tick 时间参数 t），相对位置 r(t)=(A0-B0)+t*((A1-A0)-(B1-B0))；
	/// 所以最短距离 = 原点到线段 [(A0-B0),(A1-B1)] 的距离。与 tick 执行顺序无关，且正确处理内部相交/异时误报。</summary>
	public float SegmentSegmentDistance(
		float a1x, float a1z, float a2x, float a2z,
		float b1x, float b1z, float b2x, float b2z)
	{
		// 相对运动端点：t=0 与 t=1 时的 (A-B)。线段另一端直接相减即得（见推导）。
		float r0x = a1x - b1x;
		float r0z = a1z - b1z;
		float r1x = a2x - b2x;
		float r1z = a2z - b2z;
		// 原点到相对运动线段的最短距离（复用点到线段函数）。
		return PointSegment(r0x, r0z, r1x, r1z, 0f, 0f);
	}


	/// <summary>点 (px,pz) 到线段 (x1,z1)-(x2,z2) 的最短距离。</summary>
	private float PointSegment(float x1, float z1, float x2, float z2, float px, float pz)
	{
		float dx = x2 - x1;
		float dz = z2 - z1;
		float lengthSquared = dx * dx + dz * dz;
		// 仅真正零长度（接近 float 精度）才退化为点；小幅相对位移仍走投影，避免漏判跨过碰撞半径的运动。
		if (lengthSquared < 1e-12f)
		{
			float pointDx = px - x1;
			float pointDz = pz - z1;
			return MathF.Sqrt(pointDx * pointDx + pointDz * pointDz);
		}
		float projection = ((px - x1) * dx + (pz - z1) * dz) / lengthSquared;
		projection = MathF.Max(0, MathF.Min(1, projection));
		float closestX = x1 + projection * dx;
		float closestZ = z1 + projection * dz;
		float closestDx = px - closestX;
		float closestDz = pz - closestZ;
		return MathF.Sqrt(closestDx * closestDx + closestDz * closestDz);
	}
}
