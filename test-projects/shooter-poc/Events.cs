// SPDX-License-Identifier: MIT
// Events.cs —— P2.2 Shooter PoC 事件类型（纯数据）
using Baize.Ecs;

namespace ShooterPoc;

/// <summary>伤害请求：子弹 → 敌人（EntityHandle 带代际——防 ID 复用错指）。</summary>
public readonly struct DamageRequest
{
    public readonly EntityHandle Source;   // 子弹
    public readonly EntityHandle Target;   // 敌人
    public readonly int Amount;

    public DamageRequest(EntityHandle source, EntityHandle target, int amount)
    {
        Source = source;
        Target = target;
        Amount = amount;
    }
}

/// <summary>游戏结束事件（敌人碰到主角）。</summary>
public readonly struct GameOverEvent { }
