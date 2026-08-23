// SPDX-License-Identifier: MIT
// ShooterWorld.cs —— O2 世界级辅助：对象遍历查询、命中结算、GameOver 仲裁
//
// Gameplay 通过 world.Roots/Children 遍历访问对象（O1 内核无全局 Query API；
// O2 规模小、遍历足够，批量查询留给 O6 可选后端——方案 §14.2）。

using System.Collections.Generic;
using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>世界辅助（纯静态功能，不持有状态）。</summary>
public static class ShooterWorld
{
	/// <summary>深度优先遍历全部对象（含子树）。</summary>
	public static IEnumerable<GameObject> AllObjects(GameWorld world)
	{
		foreach (var root in world.Roots)
		{
			foreach (var obj in Walk(root))
			{
				yield return obj;
			}
		}
	}

	/// <summary>按谓词查找对象（存活；含子树）。</summary>
	public static IEnumerable<GameObject> QueryObjects(GameWorld world, System.Predicate<GameObject> predicate)
	{
		foreach (var obj in AllObjects(world))
		{
			if (!obj.IsDestroyed && predicate(obj))
			{
				yield return obj;
			}
		}
	}

	/// <summary>投射物命中结算：伤害 → 死亡 → 计分（原 ResolveDamageSystem 语义）。去重由 MatchState 按 Tick 处理。</summary>
	public static bool ResolveHit(GameWorld world, GameObject source, GameObject target, int amount)
	{
		var match = world.GetService<MatchState>();
		return match.HandleProjectileHit(source, target, amount);
	}

	/// <summary>帧末清理投射物（越界）：登记到 MatchState 帧末源队列，仅 Playing 提交（reviewer P1 第五轮）。</summary>
public static void ScheduleProjectileCleanup(GameWorld world, GameObject projectile)
	{
		world.GetService<MatchState>().ScheduleSourceCleanup(projectile);
	}

	/// <summary>帧末投射物位移（TravelDistance 累计 + 越界清理）：登记，仅 Playing 提交（reviewer P1 第六轮）。</summary>
	public static void ScheduleProjectileUpdate(GameWorld world, GameObject projectile, float deltaTravel)
	{
		world.GetService<MatchState>().ScheduleProjectileTravel(projectile, deltaTravel);
	}
	/// <summary>GameOver 请求：回滚本 Tick 已即时创建的敌人/投射物，再切换阶段（原 CommandBuffer.Reset 语义）。</summary>
	public static void RequestGameOver(GameWorld world)
	{
		var match = world.GetService<MatchState>();
		if (match.Phase != GamePhase.Playing)
		{
			return;
		}
		var rollback = new List<GameObject>();
		foreach (var obj in AllObjects(world))
		{
			if (obj.SpawnTickIndex == world.TickIndex &&
				(obj.GetComponent<EnemyFaction>() != null || obj.GetComponent<ProjectileTag>() != null))
			{
				rollback.Add(obj);
			}
		}
		foreach (var obj in rollback)
		{
			if (!obj.IsDestroyed)
			{
				// 回滚的即时创建敌人需同步销毁状态（AliveEnemies 递减）。
				if (obj.GetComponent<EnemyFaction>() != null)
				{
					match.AliveEnemies = match.AliveEnemies > 0 ? match.AliveEnemies - 1 : 0;
				}
				obj.Destroy();
			}
		}
		match.Phase = GamePhase.GameOver;
	}

	/// <summary>当前是否 Playing（GameOver 冻结门禁，行为组件 OnTick 首行检查）。</summary>
	public static bool IsPlaying(GameWorld world)
	{
		return world.GetService<MatchState>().Phase == GamePhase.Playing;
	}

	private static IEnumerable<GameObject> Walk(GameObject obj)
	{
		yield return obj;
		foreach (var child in obj.Children)
		{
			foreach (var descendant in Walk(child))
			{
				yield return descendant;
			}
		}
	}
}
