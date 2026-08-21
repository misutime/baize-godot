// SPDX-License-Identifier: MIT
// Events.cs —— P2.2 Shooter PoC 事件类型（纯数据）
namespace ShooterPoc;

/// <summary>伤害请求：子弹 → 敌人。</summary>
public readonly struct DamageRequest
{
    public readonly int SourceId;   // 子弹
    public readonly int TargetId;   // 敌人
    public readonly int Amount;

    public DamageRequest(int sourceId, int targetId, int amount)
    {
        SourceId = sourceId;
        TargetId = targetId;
        Amount = amount;
    }
}

/// <summary>游戏结束事件（敌人碰到主角）。</summary>
public readonly struct GameOverEvent { }
