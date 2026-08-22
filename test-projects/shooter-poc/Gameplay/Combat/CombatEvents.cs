// SPDX-License-Identifier: MIT
// CombatEvents.cs —— 战斗系统之间的瞬时事实

using Baize.Ecs;

namespace ShooterPoc;

/// <summary>这一 Tick 发生了“投射物请求伤害目标”；句柄代际防止 Id 复用误伤。</summary>
public readonly struct DamageRequested(
	EntityHandle source,
	EntityHandle target,
	int amount)
{
	public readonly EntityHandle Source = source;
	public readonly EntityHandle Target = target;
	public readonly int Amount = amount;
}
