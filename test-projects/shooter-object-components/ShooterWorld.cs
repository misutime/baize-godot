// SPDX-License-Identifier: MIT
// ShooterWorld.cs —— O2 世界级查询辅助（纯静态遍历；无命中/计分仲裁——逻辑都在组件里）

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
