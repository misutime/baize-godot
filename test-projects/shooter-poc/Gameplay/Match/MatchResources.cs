// SPDX-License-Identifier: MIT
// MatchResources.cs —— 一局游戏的全局运行状态

namespace ShooterPoc;

public enum GamePhase { Playing, GameOver }

// Resource：它属于“这一局”，不属于任意一个实体。
public sealed class MatchState
{
	public GamePhase Phase = GamePhase.Playing;
	public int Score;
	public int AliveEnemies;
}
