// SPDX-License-Identifier: MIT
// ShooterActions.cs —— O2 行为组件（会动的组件在 RunFrame 的 Move 阶段更新；杂项在 OnTick；组件只操作 Owner 能力）
//
// 干净 GameObject-first 模型：
// - 组件直接读/改自己的 Owner 组件，组件间直接调用（如 bullet → enemy.Health.ApplyDamage）。
// - 阶段控制用 GameWorld.Paused（GameOver 时由 MatchController 设 Paused=true → O1 全局冻结），
//   组件不再逐帧检查 IsPlaying。
// - 无 ECS CommandBuffer：创建/命中/死亡都即时、直接（同步销毁 + 对象创建序 tick 天然去重）。

using System;
using Sola3d.GameObject;

namespace Shooter.Objects;

/// <summary>玩家控制器：读输入 → 计算本帧速度 → 移动（设 PreviousPosition 为旧位置，Position 累加速度）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity), typeof(MoveSpeed), typeof(PlayerInputMarker) })]
public sealed class PlayerControllerAction : GameComponent
{
	private InputService? _input;

	public override void OnCreate()
	{
		_input = World!.GetResource<InputService>();
	}

	public void Move(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		var speed = Owner!.GetComponent<MoveSpeed>()!;
		// 未启用（组件/父链禁用、已销毁）：不移动。
		if (!ShooterWorldHelper.CanTick(this))
		{
			vel.X = 0;
			vel.Z = 0;
			return;
		}
		vel.X = _input!.MoveX * speed.Value;
		vel.Z = _input.MoveZ * speed.Value;
		prev.X = pos.X;
		prev.Z = pos.Z;
		pos.X += vel.X * delta;
		pos.Z += vel.Z * delta;
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
		_input = _world!.GetResource<InputService>();
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
/// 命中即销毁本弹（无论是否致死）；越界销毁（即时，非帧末）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity), typeof(ProjectileConfig), typeof(TravelDistance), typeof(CollisionRadius), typeof(ProjectileTag) })]
public sealed class BulletAction : GameComponent
{
	private GameWorld? _world;
	private MatchController? _match;
	private CollisionResolver? _resolver;

	public override void OnCreate()
	{
		_world = World;
		_match = _world!.GetResource<MatchController>();
		_resolver = _world!.GetResource<CollisionResolver>();
	}

	public void Move(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var vel = Owner!.GetComponent<Velocity>()!;
		// 未启用（组件/父链禁用、已销毁）：不移动（保留当前位置，避免幽灵轨迹）。
		if (!ShooterWorldHelper.CanTick(this))
		{
			return;
		}
		prev.X = pos.X;
		prev.Z = pos.Z;
		pos.X += vel.X * delta;
		pos.Z += vel.Z * delta;
	}

	public void Collide(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner!.GetComponent<PreviousPosition>()!;
		var config = Owner!.GetComponent<ProjectileConfig>()!;
		var travelled = Owner!.GetComponent<TravelDistance>()!;
		var radius = Owner!.GetComponent<CollisionRadius>()!;
		if (!ShooterWorldHelper.CanTick(this))
		{
			return; // 禁用子弹不参与命中（prev==pos 退化点，避免误命中/误记射程）。
		}
		// 扫掠命中：子弹本帧 prev→pos vs 敌人本帧 prev→pos（移动阶段已各自更新，顺序无关）。
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
			var epos = enemy.GetComponent<Position>();
			var eprev = enemy.GetComponent<PreviousPosition>();
			if (epos == null || eprev == null)
			{
				continue;
			}
			float combined = radius.Value + enemyRadius.Value;
			float distance = _resolver!.SegmentSegmentDistance(
				prev.X, prev.Z, pos.X, pos.Z,
				eprev.X, eprev.Z, epos.X, epos.Z);
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
		// 射程清理（即时，非帧末）。
		float moveX = pos.X - prev.X;
		float moveZ = pos.Z - prev.Z;
		travelled.Value += MathF.Sqrt(moveX * moveX + moveZ * moveZ);
		if (travelled.Value > config.MaxRange)
		{
			Owner!.Destroy();
		}
	}

}

/// <summary>敌人控制器：寻玩家 → 移动 → 接触判定（RunFrame 的 Move 阶段调用）。</summary>
[GameComponent(Requires = new[] { typeof(Position), typeof(PreviousPosition), typeof(Velocity), typeof(MoveSpeed), typeof(CollisionRadius), typeof(EnemyFaction), typeof(SeekTargetMarker) })]
public sealed class EnemyControllerAction : GameComponent
{
	private GameWorld? _world;
	private MatchController? _match;

	public override void OnCreate()
	{
		_world = World;
		_match = _world!.GetResource<MatchController>();
	}

	/// <summary>寻玩家 → 移动 → 接触判定（移动后位置 vs 玩家位置）。</summary>
	public void Move(float delta)
	{
		var pos = Owner!.GetComponent<Position>()!;
		var prev = Owner.GetComponent<PreviousPosition>()!;
		var vel = Owner.GetComponent<Velocity>()!;
		var radius = Owner.GetComponent<CollisionRadius>()!;
		// 未启用（组件/父链禁用、已销毁）：不移动、不寻敌；prev=pos 退化为静止点，避免子弹扫掠过其旧运动段（幽灵轨迹）。
		if (!ShooterWorldHelper.CanTick(this))
		{
			vel.X = 0;
			vel.Z = 0;
			prev.X = pos.X;
			prev.Z = pos.Z;
			return;
		}
		var player = FindPlayer();
		if (player == null)
		{
			vel.X = 0;
			vel.Z = 0;
			// 无玩家可寻：prev=pos 退化为静止点，避免子弹扫掠过其旧运动段（幽灵轨迹）。
			prev.X = pos.X;
			prev.Z = pos.Z;
			return;
		}
		// 寻玩家：朝玩家当前 Position 计算速度（玩家已在本阶段先移动，故读到的是本帧终点）。
		var playerPos = player.GetComponent<Position>()!;
		float dx = playerPos.X - pos.X;
		float dz = playerPos.Z - pos.Z;
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
		prev.X = pos.X;
		prev.Z = pos.Z;
		pos.X += vel.X * delta;
		pos.Z += vel.Z * delta;
		// 接触判定（移动后位置）：
		var playerRadius = player.GetComponent<CollisionRadius>()!;
		float newDx = playerPos.X - pos.X;
		float newDz = playerPos.Z - pos.Z;
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
		_match = _world!.GetResource<MatchController>();
		_config = _world!.GetResource<SpawnConfig>();
		_state = _world!.GetResource<SpawnState>();
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

/// <summary>碰撞几何：顺序无关的扫掠距离（共享，避免重复代码；作为 Resource 注入保持组件自包含）。</summary>
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
