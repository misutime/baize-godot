// SPDX-License-Identifier: MIT
// MatchEvents.cs —— 对局生命周期事件

namespace ShooterPoc;

/// <summary>敌人与玩家接触，申请把本局切到 GameOver。</summary>
public readonly struct GameOverRequested { }
