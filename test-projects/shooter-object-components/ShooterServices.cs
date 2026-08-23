// SPDX-License-Identifier: MIT
// ShooterServices.cs —— O2 全局服务（GameWorld.Services 承载：对局控制器 / 输入 / 生成配置）
//
// 干净 GameObject-first：服务只做「全局状态持有者 + 阶段切换」，不做命中/计分仲裁。
// - MatchController：持有 Phase/Score/AliveEnemies；RequestGameOver 设 GameWorld.Paused=true 全局冻结
//   （O1 Paused 语义：所有组件 OnTick 停，等效 ECS RunInState(Playing) 门禁，组件无需自查）。
// - 命中/死亡由组件间直接调用（bullet → Health.ApplyDamage → MatchController.OnEnemyKilled）。

using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>对局阶段。</summary>
public enum GamePhase
{
	Playing,
	GameOver,
}

/// <summary>对局控制器（服务）：全局状态 + 阶段切换。命中计分由组件触发（OnEnemyKilled），不自足仲裁。</summary>
public sealed class MatchController
{
public GamePhase Phase { get; private set; } = GamePhase.Playing;
	// Score/AliveEnemies 可写：正常由 OnEnemyKilled/OnEnemySpawned 改；测试可预设断言。
	public int Score { get; set; }
	public int AliveEnemies { get; set; }

	private GameWorld? _world;

	/// <summary>绑定世界（Install 时设置；Paused 冻结用）。</summary>
	public void Bind(GameWorld world) => _world = world;

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

	/// <summary>请求结束对局（幂等：Playing 才切；切后设世界 Paused 冻结全局）。</summary>
	public void RequestGameOver()
	{
		if (Phase != GamePhase.Playing)
		{
			return;
		}
		Phase = GamePhase.GameOver;
		// 冻结全局（O1 Paused：所有组件 OnTick 停，等效 ECS 阶段门禁）。
		if (_world != null)
		{
			_world.Paused = true;
		}
	}

	/// <summary>重置（重启一局用；Paused 由 GameWorld.Reset 归零）。</summary>
	public void Reset()
	{
		Phase = GamePhase.Playing;
		Score = 0;
		AliveEnemies = 0;
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
