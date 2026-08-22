// SPDX-License-Identifier: MIT
// CombatComponents.cs —— 战斗能力的状态、参数与标签

using Friflo.Engine.ECS;

namespace ShooterPoc;

// 运行状态：会随玩法推进而改变。
public struct Health : IComponent { public int Current, Max; }
public struct Cooldown : IComponent { public float Remaining; }
public struct TravelDistance : IComponent { public float Value; }

// 每实体参数：设计者调数值，运行时不把倒计时写回这里。
public struct WeaponConfig : IComponent
{
	public float CooldownSeconds;
	public float ProjectileSpeed;
}

public struct ProjectileConfig : IComponent
{
	public int Damage;
	public float MaxRange;
}

public struct CollisionRadius : IComponent { public float Value; }

// 标签关系：它参与“投射物命中敌方”的规则；伤害与射程仍由参数组件表达。
public struct ProjectileTag : ITag { }
