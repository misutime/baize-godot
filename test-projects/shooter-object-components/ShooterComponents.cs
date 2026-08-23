// SPDX-License-Identifier: MIT
// ShooterComponents.cs —— O2 玩法数据组件（组件即能力：数据 + 自包含行为方法，直接操作 Owner）

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

/// <summary>控制器为一个 tick 提交的瞬态唯一运动计划；移动与碰撞只消费这里的同一条线段。</summary>
[GameComponent]
public sealed class MotionPlan : GameComponent
{
	public ulong TickIndex { get; private set; }

	public float StartX { get; private set; }

	public float StartZ { get; private set; }

	public float EndX { get; private set; }

	public float EndZ { get; private set; }

	public void Set(ulong tickIndex, float startX, float startZ, float endX, float endZ)
	{
		TickIndex = tickIndex;
		StartX = startX;
		StartZ = startZ;
		EndX = endX;
		EndZ = endZ;
	}
}

/// <summary>控制器提交的本帧速度（同时用于生成 MotionPlan）。</summary>
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

/// <summary>血量（数据 + 行为合一：ApplyDamage 直接判断死亡并销毁 Owner）。</summary>
[GameComponent]
public sealed class Health : GameComponent
{
	[GameProperty]
	public int Current { get; set; } = 1;

	[GameProperty]
	public int Max { get; set; } = 1;

	/// <summary>扣血；流归零则销毁 Owner 并返回 true（死亡）。组件直接操作 Owner，不依赖全局仲裁器。</summary>
public bool ApplyDamage(int amount)
	{
		// 先校验 Owner 存活（reviewer P2：已脱离 Owner 的组件不应再修改状态）。
		if (Owner == null || Owner.IsDestroyed)
		{
			return false;
		}
		Current -= amount;
		if (Current > 0)
		{
			return false;
		}
		Owner.Destroy();
		return true;
	}
}

/// <summary>武器冷却（运行状态；由 WeaponBehavior 写）。</summary>
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

/// <summary>已飞行距离（BulletBehavior 累计；越界销毁）。</summary>
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

// ---------- 标记/标签（取代 ECS Tag） ----------

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

/// <summary>能力标记：可读玩家输入（PlayerController 身份）。</summary>
[GameComponent]
public sealed class PlayerInputMarker : GameComponent
{
}

/// <summary>能力标记：会寻敌（EnemyController 身份）。</summary>
[GameComponent]
public sealed class SeekTargetMarker : GameComponent
{
}
