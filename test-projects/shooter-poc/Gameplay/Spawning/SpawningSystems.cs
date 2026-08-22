// SPDX-License-Identifier: MIT
// SpawningSystems.cs —— 生成规则只读配置，节拍写入独立运行状态

using Baize.Ecs;

namespace ShooterPoc;

public sealed class SpawnEnemiesSystem : EcsSystem
{
	public SpawnEnemiesSystem() => RunInState<MatchState>(GamePhase.Playing);

	protected override void Execute()
	{
		MatchState match = State<MatchState>();
		SpawnConfig config = Res<SpawnConfig>();
		SpawnState state = Res<SpawnState>();
		state.Remaining -= Tick.deltaTime;
		if (state.Remaining > 0) return;
		state.Remaining = config.Interval;

		if (match.AliveEnemies >= config.MaxAlive) return;

		// 教学场景固定从 +Z 生成，让“输入→射击→命中→得分”容易观察。
		World.CommandBuffer.Spawn(new EnemyBundle(0, config.SpawnRadius));
		match.AliveEnemies++;
	}
}
