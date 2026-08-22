// SPDX-License-Identifier: MIT
// EnemySystems.cs —— 寻敌能力与接触规则；没有 EnemyAI 大杂烩组件

using System;
using Baize.Ecs;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace ShooterPoc;

public sealed class SeekPlayerSystem : QuerySystem<Position, Velocity, SeekTarget, MoveSpeed>
{
	private readonly EcsWorld _world;

	public SeekPlayerSystem(EcsWorld world)
	{
		_world = world;
		Filter.AllTags(Tags.Get<EnemyFaction>());
	}

	protected override void OnUpdate()
	{
		var match = _world.GetResource<MatchState>();
		bool foundPlayer = TryGetPlayerPosition(out Position playerPosition);

		Query.ForEachEntity((ref Position position, ref Velocity velocity,
			ref SeekTarget _, ref MoveSpeed speed, Entity entity) =>
		{
			if (match.Phase != GamePhase.Playing || !foundPlayer)
			{
				velocity.X = 0;
				velocity.Z = 0;
				return;
			}

			float dx = playerPosition.X - position.X;
			float dz = playerPosition.Z - position.Z;
			float length = MathF.Sqrt(dx * dx + dz * dz);
			if (length <= 0.01f)
			{
				velocity.X = 0;
				velocity.Z = 0;
				return;
			}

			velocity.X = dx / length * speed.Value;
			velocity.Z = dz / length * speed.Value;
		});
	}

	private bool TryGetPlayerPosition(out Position position)
	{
		foreach (var player in _world.Store.Query<Position>()
					 .AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			position = player.GetComponent<Position>();
			return true;
		}

		position = default;
		return false;
	}
}

public sealed class EnemyContactSystem : QuerySystem<Position, CollisionRadius>
{
	private readonly EcsWorld _world;

	public EnemyContactSystem(EcsWorld world)
	{
		_world = world;
		Filter.AllTags(Tags.Get<EnemyFaction>());
	}

	protected override void OnUpdate()
	{
		if (_world.GetResource<MatchState>().Phase != GamePhase.Playing) return;

		foreach (var player in _world.Store.Query<Position, CollisionRadius>()
					 .AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			Position playerPosition = player.GetComponent<Position>();
			CollisionRadius playerRadius = player.GetComponent<CollisionRadius>();

			Query.ForEachEntity((ref Position position, ref CollisionRadius radius, Entity entity) =>
			{
				float dx = playerPosition.X - position.X;
				float dz = playerPosition.Z - position.Z;
				float contactDistance = playerRadius.Value + radius.Value;
				if (MathF.Sqrt(dx * dx + dz * dz) <= contactDistance)
				{
					_world.Events.Writer<GameOverRequested>().Send(default);
				}
			});
		}
	}
}
