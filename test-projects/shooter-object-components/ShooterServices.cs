// SPDX-License-Identifier: MIT
// ShooterServices.cs —— O2 全局服务（GameWorld.Services 承载：对局/输入/生成配置）

using System;
using System.Collections.Generic;
using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>对局阶段（原 ECS GamePhase）。</summary>
public enum GamePhase
{
	Playing,
	GameOver,
}

/// <summary>对局状态（服务）：得分、存活敌人、阶段。命中/计分/销毁/射程清理全部帧末提交（FlushFrame），
/// 仅 Phase 仍 Playing 时应用；GameOver 整帧丢弃（参考版 EndMatchSystem 先于 Resolve/Cleanup）。</summary>
public sealed class MatchState
{
	public GamePhase Phase { get; set; } = GamePhase.Playing;
	public int Score;
	public int AliveEnemies;

	// 去重：记录"本 Tick 已结算的目标"（实体句柄）。Tick 变化时清空。
	private ulong _lastTick = ulong.MaxValue;
	private readonly HashSet<GameObject> _hitTargetsThisTick = new();

	// 帧末待提交：命中源（投射物，删除）、目标伤害、投射物位移（TravelDistance）。
	private readonly List<GameObject> _frameSources = new();
	private readonly Dictionary<GameObject, int> _frameDamage = new();
	private readonly Dictionary<GameObject, float> _frameProjectileTravel = new();

	public void Reset()
	{
		Phase = GamePhase.Playing;
		Score = 0;
		AliveEnemies = 0;
		_hitTargetsThisTick.Clear();
		_lastTick = ulong.MaxValue;
		_frameSources.Clear();
		_frameDamage.Clear();
		_frameProjectileTravel.Clear();
	}

	/// <summary>投射物命中（帧末提交模型）：过滤 + Tick 去重，登记到帧末队列（不立即改 Health/Score/销毁）。
	/// 帧末由 ShooterGame.Step 调 FlushFrame——仅当 Phase 仍 Playing 才提交，GameOver 整帧丢弃。
	/// 返回是否发生了命中登记（非去重命中）。</summary>
	public bool HandleProjectileHit(GameObject source, GameObject target, int amount)
	{
		if (source.GetComponent<ProjectileTag>() == null || target.GetComponent<EnemyFaction>() == null)
		{
			return false;
		}
		// 命中源（投射物）帧末必被消费（命中即使去重/非致死，源也删除）。
		if (!_frameSources.Contains(source))
		{
			_frameSources.Add(source);
		}
		// 同 Tick 同一目标只结算一次（防多个投射物重复消费同一次死亡）。
		ulong tickNow = source.World.TickIndex;
		if (_lastTick != tickNow)
		{
			_lastTick = tickNow;
			_hitTargetsThisTick.Clear();
		}
		if (!_hitTargetsThisTick.Add(target))
		{
			return false;
		}
		var health = target.GetComponent<Health>();
		if (health == null)
		{
			return false;
		}
		// 帧末提交：登记累计伤害。
		_frameDamage[target] = (_frameDamage.TryGetValue(target, out var existing) ? existing : 0) + amount;
		return true;
	}

	/// <summary>帧末清理投射物（越界未命中）：登记源，仅 Playing 提交（reviewer P1 第五轮：GameOver 帧不执行 Cleanup）。</summary>
	public void ScheduleSourceCleanup(GameObject projectile)
	{
		if (!_frameSources.Contains(projectile))
		{
			_frameSources.Add(projectile);
		}
	}

	/// <summary>帧末投射物位移：登记 TravelDistance 增量，仅 Playing 提交（reviewer P1 第六轮：GameOver 帧不累计距离）。</summary>
	public void ScheduleProjectileTravel(GameObject projectile, float deltaTravel)
	{
		_frameProjectileTravel[projectile] = (_frameProjectileTravel.TryGetValue(projectile, out var existing) ? existing : 0) + deltaTravel;
	}

	/// <summary>本帧待提交的命中源数（投射物；帧末决定是否删除）。</summary>
	public int PendingDestroyCount => _frameSources.Count;

	/// <summary>帧末提交：Phase 仍 Playing 才应用距离/伤害/计分/销毁；GameOver 整帧丢弃。由 ShooterGame.Step 在 world.Tick 后调用。</summary>
	public void FlushFrame(GameWorld world)
	{
		if (Phase != GamePhase.Playing)
		{
			// GameOver：丢弃整帧待提交命中 + 距离增量（参考版 EndMatchSystem 先于 Cleanup/Resolve）。
			_frameSources.Clear();
			_frameDamage.Clear();
			_frameProjectileTravel.Clear();
			return;
		}

		// 帧末应用于 TravelDistance 累计 + 越界清理（与 Cleanup 一起仅 Playing 提交）。
		foreach (var projectile in _frameProjectileTravel.Keys)
		{
			if (projectile.IsDestroyed)
			{
				continue;
			}
			var travelled = projectile.GetComponent<TravelDistance>();
			var config = projectile.GetComponent<ProjectileConfig>();
			if (travelled == null || config == null)
			{
				continue;
			}
			travelled.Value += _frameProjectileTravel[projectile];
			if (travelled.Value > config.MaxRange && !_frameSources.Contains(projectile))
			{
				_frameSources.Add(projectile);
			}
		}
		_frameProjectileTravel.Clear();

		// 销毁命中/越界源（投射物）。
		foreach (var source in _frameSources)
		{
			if (!source.IsDestroyed)
			{
				source.Destroy();
			}
		}
		_frameSources.Clear();

		// 应用累计伤害。
		var destroyed = new HashSet<GameObject>();
		foreach (var target in _frameDamage.Keys)
		{
			if (target.IsDestroyed)
			{
				continue;
			}
			var health = target.GetComponent<Health>();
			if (health == null)
			{
				continue;
			}
			health.Current -= _frameDamage[target];
		}
		_frameDamage.Clear();

		// 只提交真正死亡的目标（应用累计伤害后 Current<=0 才销毁并计分；非致死保留不计分）。
		foreach (var target in _hitTargetsThisTick)
		{
			if (target.IsDestroyed || destroyed.Contains(target))
			{
				continue;
			}
			var health = target.GetComponent<Health>();
			if (health == null || health.Current > 0)
			{
				continue;
			}
			bool isEnemy = target.GetComponent<EnemyFaction>() != null;
			target.Destroy();
			destroyed.Add(target);
			if (isEnemy)
			{
				Score++;
				if (AliveEnemies > 0)
				{
					AliveEnemies--;
				}
			}
		}
	}
}

/// <summary>本帧输入（服务；测试/回放直接写字段）：移动 + 射击边沿。WasPressed 是上一帧射击态。</summary>
public sealed class InputService
{
	public float MoveX;
	public float MoveZ;
	public bool FirePressed;
	public bool WasPressed;

	/// <summary>本帧是否第一次按下（边沿触发，防连发）。</summary>
	public bool ConsumeFireEdge()
	{
		bool edge = FirePressed && !WasPressed;
		WasPressed = FirePressed;
		return edge;
	}

	public void Reset()
	{
		MoveX = 0;
		MoveZ = 0;
		FirePressed = false;
		WasPressed = false;
	}
}

/// <summary>生成配置（服务；只描述规则，不缓存状态）。</summary>
public sealed class SpawnConfig
{
	public float Interval = 1.0f;
	public int MaxAlive = 10;
	public float SpawnRadius = 20.0f;
}

/// <summary>生成运行状态（服务；全球一个生成节拍）。</summary>
public sealed class SpawnState
{
	public float Remaining;
}

/// <summary>确定性随机（SplitMix64 风格，同 TickIndex 同结果——与 ECS 版口径一致）。</summary>
internal static class DeterministicRandom
{
	public static ulong HashTick(ulong tickIndex)
	{
		unchecked
		{
			ulong value = tickIndex + 0x9E37_79B9_7F4A_7C15UL;
			value = (value ^ (value >> 30)) * 0xBF58_476D_1CE4_E5B9UL;
			value = (value ^ (value >> 27)) * 0x94D0_49BB_1331_11EBUL;
			return value ^ (value >> 31);
		}
	}
}
