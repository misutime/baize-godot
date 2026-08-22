// SPDX-License-Identifier: MIT
// MatchSystems.cs —— 对局结束事件只在这里改变 MatchState

using Baize.Ecs;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace ShooterPoc;

public sealed class EndMatchSystem : BaseSystem
{
	private readonly EcsWorld _world;

	public EndMatchSystem(EcsWorld world) => _world = world;

	protected override void OnUpdateGroup()
	{
		var match = _world.GetResource<MatchState>();
		if (match.Phase == GamePhase.GameOver) return;
		if (_world.Events.Reader<GameOverRequested>().Consume() == 0) return;

		match.Phase = GamePhase.GameOver;

		// 丢弃结束前排队的生成/射击，并冻结现存实体。
		_world.CommandBuffer.Reset();
		match.AliveEnemies = 0;
		foreach (var entity in _world.Store.Entities)
		{
			if (entity.Tags.Has<EnemyFaction>()) match.AliveEnemies++;
			if (!entity.HasComponent<Velocity>()) continue;

			ref Velocity velocity = ref entity.GetComponent<Velocity>();
			velocity.X = 0;
			velocity.Z = 0;
		}
	}
}
