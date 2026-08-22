// SPDX-License-Identifier: MIT
// EnemySystems.cs —— 寻敌能力与接触规则；没有 EnemyAI 大杂烩组件

using System;
using Baize.Ecs;
using Friflo.Engine.ECS;

namespace ShooterPoc;

public sealed class SeekPlayerSystem : EcsSystem<Position, Velocity, SeekTarget, MoveSpeed>
{
	public SeekPlayerSystem()
	{
		RunInState<MatchState>(GamePhase.Playing);
		Filter.AllTags(Tags.Get<EnemyFaction>());
	}

	protected override void Execute()
	{
		bool foundPlayer = TryGetPlayerPosition(out Position playerPosition);
		Query.ForEachEntity((ref Position position, ref Velocity velocity,
			ref SeekTarget _, ref MoveSpeed speed, Entity entity) =>
		{
			if (!foundPlayer)
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
		foreach (var player in World.Store.Query<Position>()
					 .AllTags(Tags.Get<PlayerFaction>()).Entities)
		{
			position = player.GetComponent<Position>();
			return true;
		}

		position = default;
		return false;
	}
}

public sealed class EnemyContactSystem : EcsSystem<Position, CollisionRadius>
{
	public EnemyContactSystem()
	{
		RunInState<MatchState>(GamePhase.Playing);
		Filter.AllTags(Tags.Get<EnemyFaction>());
	}

	protected override void Execute()
	{
		EventWriter<GameOverRequested> writer = WriteEvents<GameOverRequested>();
		foreach (var player in World.Store.Query<Position, CollisionRadius>()
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
					writer.Send(default);
				}
			});
		}
	}
}
