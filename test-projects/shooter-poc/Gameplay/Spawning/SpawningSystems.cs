// SPDX-License-Identifier: MIT
// SpawningSystems.cs —— 生成规则只读配置，节拍写入独立运行状态

using Baize.Ecs;
using Friflo.Engine.ECS.Systems;

namespace ShooterPoc;

public sealed class SpawnEnemiesSystem : BaseSystem
{
	private readonly EcsWorld _world;

	public SpawnEnemiesSystem(EcsWorld world) => _world = world;

	protected override void OnUpdateGroup()
	{
		var match = _world.GetResource<MatchState>();
		if (match.Phase != GamePhase.Playing) return;

		var config = _world.GetResource<SpawnConfig>();
		var state = _world.GetResource<SpawnState>();
		state.Remaining -= Tick.deltaTime;
		if (state.Remaining > 0) return;
		state.Remaining = config.Interval;

		if (match.AliveEnemies >= config.MaxAlive) return;

		// 教学场景固定从 +Z 生成，让“输入→射击→命中→得分”容易观察。
		_world.CommandBuffer.Spawn(new EnemyBundle(0, config.SpawnRadius));
		match.AliveEnemies++;
	}
}
