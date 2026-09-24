// SPDX-License-Identifier: MIT
// EnemySystems.cs —— 寻敌能力与接触规则；没有 EnemyAI 大杂烩组件

using System;
using Baize.Ecs;
using Friflo.Engine.ECS;

namespace Shooter.Gameplay;

public sealed class SeekPlayerSystem : EcsSystem<Position, Velocity, SeekTarget, MoveSpeed>
{
	public SeekPlayerSystem()
	{
		RunInState<MatchState>(GamePhase.Playing);
		ForTag<EnemyFaction>();
	}

	protected override void Execute()
	{
		bool foundPlayer = TryGetPlayerPosition(out Position playerPosition);
		ForEach((ref Position position, ref Velocity velocity,
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
		foreach (var (playerPosition, _) in Read<Position>().WithTag<PlayerFaction>())
		{
			position = playerPosition;
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
		ForTag<EnemyFaction>();
	}

	protected override void Execute()
	{
		EventWriter<GameOverRequested> writer = WriteEvents<GameOverRequested>();
		foreach (var (playerPosition, playerRadius, _) in
			Read<Position, CollisionRadius>().WithTag<PlayerFaction>())
		{
			ForEach((ref Position position, ref CollisionRadius radius, Entity entity) =>
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
