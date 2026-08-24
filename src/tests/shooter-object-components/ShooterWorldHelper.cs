// SPDX-License-Identifier: MIT
// ShooterWorldHelper.cs —— O2 世界级查询辅助（纯静态遍历；无命中/计分仲裁——逻辑都在组件里）

using System.Collections.Generic;
using Sola3d.GameObject;
namespace Shooter.Objects;


/// <summary>世界辅助（纯静态功能，不持有状态）。</summary>
public static class ShooterWorldHelper
{
	/// <summary>深度优先遍历全部对象（含子树）。</summary>
	public static IEnumerable<GameObject> AllObjects(GameWorld world)
	{
		// 快照一次性物化：命中/越界会即时销毁对象，遍历期间结构变更不破坏枚举（对齐引擎 Tick 快照语义）。
		var result = new List<GameObject>();
		foreach (var root in world.Roots)
		{
			Walk(root, result);
		}
		return result;
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
		// 等效 O1 IsTickable：世界暂停（Paused）→ 不参与；对象/父链 Enabled + 组件 Enabled 全通过。
		if (comp.World!.Paused)
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

	/// <summary>阶段1 Move：所有"会动"的对象先移动到本帧终点（玩家 → 敌人 → 子弹）。</summary>
	public static void MoveAll(GameWorld world, float delta)
	{
		foreach (var obj in QueryObjects(world, o => o.GetComponent<PlayerControllerAction>() != null))
		{
			obj.GetComponent<PlayerControllerAction>()!.Move(delta);
		}
		foreach (var obj in QueryObjects(world, o => o.GetComponent<EnemyControllerAction>() != null))
		{
			obj.GetComponent<EnemyControllerAction>()!.Move(delta);
		}
		foreach (var obj in QueryObjects(world, o => o.GetComponent<BulletAction>() != null))
		{
			obj.GetComponent<BulletAction>()!.Move(delta);
		}
	}

	/// <summary>阶段2 Collide：子弹做扫掠命中（读双方本帧 prev→pos，顺序无关）。</summary>
	public static void CollideAll(GameWorld world, float delta)
	{
		foreach (var obj in QueryObjects(world, o => o.GetComponent<BulletAction>() != null))
		{
			obj.GetComponent<BulletAction>()!.Collide(delta);
		}
	}


	private static void Walk(GameObject obj, List<GameObject> result)
	{
		result.Add(obj);
		foreach (var child in obj.Children)
		{
			Walk(child, result);
		}
	}
}
