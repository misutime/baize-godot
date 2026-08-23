// SPDX-License-Identifier: MIT
// ShooterResources.cs —— O2 全局资源（GameWorld.Resources 承载：对局控制器 / 输入 / 生成配置）
//
// 干净 GameObject-first：全局资源只做「全局状态持有者 + 阶段切换」，不做命中/计分仲裁。
// - MatchController：持有 Phase/Score/AliveEnemies；RequestGameOver 只切 Phase（纯状态，不碰世界）。
// - 冻结由组合根（ShooterGame.RunFrame）读各来源状态汇聚（O1 Paused 语义：所有组件 OnTick 停）。

using System.Collections.Generic;
using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>对局阶段。</summary>
public enum GamePhase
{
	Playing,
	GameOver,
}

/// <summary>对局控制器（资源）：全局状态 + 阶段切换。命中计分由组件触发（OnEnemyKilled），不自足仲裁。</summary>
public sealed class MatchController
{
	public GamePhase Phase { get; private set; } = GamePhase.Playing;
	// Score/AliveEnemies 可写：正常由 OnEnemyKilled/OnEnemySpawned 改；测试可预设断言。
	public int Score { get; set; }
	public int AliveEnemies { get; set; }

	/// <summary>敌人出生：AliveEnemies++（由生成器调用）。</summary>
	public void OnEnemySpawned() => AliveEnemies++;

	/// <summary>敌人死亡：计分 + AliveEnemies--（由命中方调用）。</summary>
	public void OnEnemyKilled()
	{
		Score++;
		if (AliveEnemies > 0)
		{
			AliveEnemies--;
		}
	}

	/// <summary>请求结束对局（幂等：Playing 才切；只切换状态，不碰世界）。冻结由组合根读状态汇聚。</summary>
	public void RequestGameOver()
	{
		if (Phase != GamePhase.Playing)
		{
			return;
		}
		Phase = GamePhase.GameOver;
	}

	/// <summary>重置（重启一局用；Paused 由 GameWorld.Reset 归零）。</summary>
	public void Reset()
	{
		Phase = GamePhase.Playing;
		Score = 0;
		AliveEnemies = 0;
	}
}

/// <summary>多来源暂停计数（资源）：终局/菜单等来源各自 Pause/Unpause；来源名去重（重复 Pause 不叠加），任一来源 active 即 IsPaused。</summary>
public sealed class PauseManager
{
	private readonly HashSet<string> _active = new();

	public bool IsPaused => _active.Count > 0;

	/// <summary>请求暂停（来源名幂等：已 active 则不叠加）。</summary>
	public void Pause(string source)
	{
		if (source != null) _active.Add(source);
	}

	/// <summary>解除暂停（来源名不存在则忽略）。</summary>
	public void Unpause(string source)
	{
		if (source != null) _active.Remove(source);
	}

	/// <summary>清空全部暂停来源（重启一局用）。</summary>
	public void Clear() => _active.Clear();
}

/// <summary>本帧输入（资源；测试/回放直接写字段）：移动 + 射击边沿。WasPressed 是上一帧射击态。</summary>
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

/// <summary>生成配置（资源；只描述规则，不缓存状态）。</summary>
public sealed class SpawnConfig
{
	public float Interval = 1.0f;
	public int MaxAlive = 10;
	public float SpawnRadius = 20.0f;
}

/// <summary>生成运行状态（资源；全球一个生成节拍）。</summary>
public sealed class SpawnState
{
	public float Remaining;
}

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
