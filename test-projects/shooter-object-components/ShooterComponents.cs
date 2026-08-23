// SPDX-License-Identifier: MIT
// ShooterComponents.cs —— O2 玩法数据组件与标记组件（方案 §14.2：普通组件有数据/标记；行为由行为组件承担）

using Baize.GameObject;

namespace Shooter.Objects;
// ---------- 移动 ----------

/// <summary>位置（X/Z 平面）。</summary>
[GameComponent]
public sealed class Position : GameComponent
{
	[GameProperty]
	public float X { get; set; }

	[GameProperty]
	public float Z { get; set; }
}

/// <summary>上一帧位置（扫掠命中用）。</summary>
[GameComponent]
public sealed class PreviousPosition : GameComponent
{
	[GameProperty]
	public float X { get; set; }

	[GameProperty]
	public float Z { get; set; }
}

/// <summary>速度（每帧更新，MoveSystem 消费）。</summary>
[GameComponent]
public sealed class Velocity : GameComponent
{
	[GameProperty]
	public float X { get; set; }

	[GameProperty]
	public float Z { get; set; }
}

/// <summary>每实体参数：移动速度。</summary>
[GameComponent]
public sealed class MoveSpeed : GameComponent
{
	[GameProperty]
	public float Value { get; set; } = 3.5f;
}

// ---------- 战斗 ----------

/// <summary>血量（运行状态；Current 由战斗结算写）。</summary>
[GameComponent]
public sealed class Health : GameComponent
{
	[GameProperty]
	public int Current { get; set; } = 1;

	[GameProperty]
	public int Max { get; set; } = 1;
}

/// <summary>武器冷却（运行状态；由 FireWeaponSystem 写）。</summary>
[GameComponent]
public sealed class Cooldown : GameComponent
{
	[GameProperty]
	public float Remaining { get; set; }
}

/// <summary>每实体参数：武器配置。</summary>
[GameComponent]
public sealed class WeaponConfig : GameComponent
{
	[GameProperty]
	public float CooldownSeconds { get; set; } = 0.3f;

	[GameProperty]
	public float ProjectileSpeed { get; set; } = 30.0f;
}

/// <summary>每实体参数：投射物配置。</summary>
[GameComponent]
public sealed class ProjectileConfig : GameComponent
{
	[GameProperty]
	public int Damage { get; set; } = 1;

	[GameProperty]
	public float MaxRange { get; set; } = 50.0f;
}

/// <summary>可序列化属性：已飞行距离（CleanupProjectiles 写）。</summary>
[GameComponent]
public sealed class TravelDistance : GameComponent
{
	[GameProperty]
	public float Value { get; set; }
}

/// <summary>每实体参数：碰撞半径。</summary>
[GameComponent]
public sealed class CollisionRadius : GameComponent
{
	[GameProperty]
	public float Value { get; set; } = 0.5f;
}

// ---------- 标记/标签（取代 ECS Tag；方案 §14.2：标签关系 ← 标记组件） ----------

/// <summary>标记：玩家阵营。</summary>
[GameComponent]
public sealed class PlayerFaction : GameComponent
{
}

/// <summary>标记：敌人阵营。</summary>
[GameComponent]
public sealed class EnemyFaction : GameComponent
{
}

/// <summary>标记：投射物。</summary>
[GameComponent]
public sealed class ProjectileTag : GameComponent
{
}

/// <summary>能力标记：可读玩家输入（FiresWeapon 的宿主身份）。</summary>
[GameComponent]
public sealed class PlayerInputMarker : GameComponent
{
}

/// <summary>能力标记：会寻敌（EnemyAI 行为附着对象）。</summary>
[GameComponent]
public sealed class SeekTargetMarker : GameComponent
{
}
