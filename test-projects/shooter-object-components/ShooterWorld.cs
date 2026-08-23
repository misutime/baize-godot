// SPDX-License-Identifier: MIT
// ShooterWorld.cs —— O2 世界级查询辅助（纯静态遍历；无命中/计分仲裁——逻辑都在组件里）

using System.Collections.Generic;
using Baize.GameObject;
namespace Shooter.Objects;

/// <summary>运动规划阶段：Step 在 Tick 前按此顺序让各控制器提交本帧运动计划。
/// 顺序即游戏语义（玩家先规划，敌人据玩家本帧终点规划，子弹提交自身线段），
/// 只用于编排，不构成全局排程器。</summary>
public enum PlanPhase
{
	PlayerInput,
	Enemy,
	Projectile,
}

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
	/// <summary>组件本帧是否应参与规划（等效 O1 IsTickable：对象存活 + 对象与父链有效启用 + 组件启用）。</summary>
	public static bool CanTick(GameComponent comp)
	{
		if (comp.Owner == null || comp.Owner.IsDestroyed)
		{
			return false;
		}
		if (!comp.Enabled)
		{
			return false;
		}
		for (GameObject? obj = comp.Owner; obj != null; obj = obj.Parent)
		{
			if (obj.IsDestroyed || !obj.Enabled)
			{
				return false;
			}
		}
		return true;
	}

	/// <summary>按阶段提交运动计划：只会为该阶段对应的行为组件调用 PlanMotion，
	/// 顺序由 <see cref="PlanPhase"/> 声明序决定（无全局排程器）。</summary>
	public static void PlanMotion(GameWorld world, float delta, ulong tickIndex, PlanPhase phase)
	{
		switch (phase)
		{
			case PlanPhase.PlayerInput:
				foreach (var obj in QueryObjects(world, o => o.GetComponent<PlayerControllerAction>() != null))
				{
					obj.GetComponent<PlayerControllerAction>()!.PlanMotion(delta, tickIndex);
				}
				break;
			case PlanPhase.Enemy:
				foreach (var obj in QueryObjects(world, o => o.GetComponent<EnemyControllerAction>() != null))
				{
					obj.GetComponent<EnemyControllerAction>()!.PlanMotion(delta, tickIndex);
				}
				break;
			case PlanPhase.Projectile:
				foreach (var obj in QueryObjects(world, o => o.GetComponent<BulletAction>() != null))
				{
					obj.GetComponent<BulletAction>()!.PlanMotion(delta, tickIndex);
				}
				break;
		}
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
