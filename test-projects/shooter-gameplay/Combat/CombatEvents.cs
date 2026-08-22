// SPDX-License-Identifier: MIT
// CombatEvents.cs —— 战斗系统之间的瞬时事实

using Friflo.Engine.ECS;

namespace Shooter.Gameplay;

/// <summary>这一 Tick 发生了“投射物请求伤害目标”；Friflo Entity 自带 Store + Revision，可防止 Id 复用误伤。</summary>
public readonly struct DamageRequested(
	Entity source,
	Entity target,
	int amount)
{
	public readonly Entity Source = source;
	public readonly Entity Target = target;
	public readonly int Amount = amount;
}
