// SPDX-License-Identifier: MIT
// MatchSystems.cs —— 对局结束事件只在这里触发声明式状态转换

using Sola3d.Ecs;

namespace Shooter.Gameplay;

public sealed class EndMatchSystem : EcsSystem
{
	public EndMatchSystem() => RunInState<MatchState>(GamePhase.Playing);

	protected override void Execute()
	{
		if (ReadEvents<GameOverRequested>().Consume() == 0) return;
		State<MatchState>().TransitionTo(GamePhase.GameOver);
	}
}
