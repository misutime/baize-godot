// SPDX-License-Identifier: MIT
// ShooterSnapshots.cs —— 纯 .NET 的只读表现契约；不依赖 Godot，也不反向修改 Gameplay

using System;
using System.Collections.Generic;
using Sola3d.Ecs;
using Friflo.Engine.ECS;

namespace Shooter.Gameplay;

public enum RenderKind
{
	Player,
	Enemy,
	Projectile,
}

public readonly record struct RenderEntitySnapshot(
	int EntityId,
	int Revision,
	RenderKind Kind,
	float X,
	float Z);

public sealed class RenderSnapshot
{
	public static RenderSnapshot Empty { get; } = new([], [], []);

	public RenderEntitySnapshot[] Players { get; }
	public RenderEntitySnapshot[] Enemies { get; }
	public RenderEntitySnapshot[] Projectiles { get; }

	public RenderSnapshot(
		RenderEntitySnapshot[] players,
		RenderEntitySnapshot[] enemies,
		RenderEntitySnapshot[] projectiles)
	{
		Players = players;
		Enemies = enemies;
		Projectiles = projectiles;
	}

	public bool TryFind(RenderEntitySnapshot current, out RenderEntitySnapshot previous)
	{
		RenderEntitySnapshot[] candidates = current.Kind switch
		{
			RenderKind.Player => Players,
			RenderKind.Enemy => Enemies,
			RenderKind.Projectile => Projectiles,
			_ => Array.Empty<RenderEntitySnapshot>(),
		};

		foreach (RenderEntitySnapshot candidate in candidates)
		{
			if (candidate.EntityId != current.EntityId || candidate.Revision != current.Revision) continue;
			previous = candidate;
			return true;
		}

		previous = current;
		return false;
	}
}

public readonly record struct HudSnapshot(
	int Score,
	int AliveEnemies,
	GamePhase Phase,
	ulong TickIndex);

public sealed class ShooterFrameSnapshot
{
	public static ShooterFrameSnapshot Empty { get; } = new(
		RenderSnapshot.Empty,
		new HudSnapshot(0, 0, GamePhase.Playing, 0));

	public RenderSnapshot Render { get; }
	public HudSnapshot Hud { get; }

	public ShooterFrameSnapshot(RenderSnapshot render, HudSnapshot hud)
	{
		Render = render;
		Hud = hud;
	}
}

public sealed class ShooterSnapshotExtractor
{
	public ShooterFrameSnapshot Extract(EcsWorld world, ulong? completedTickIndex = null)
	{
		var players = new List<RenderEntitySnapshot>(1);
		var enemies = new List<RenderEntitySnapshot>();
		var projectiles = new List<RenderEntitySnapshot>();

		foreach (Entity entity in world.Store.Entities)
		{
			if (!entity.HasComponent<Position>()) continue;
			ref Position position = ref entity.GetComponent<Position>();

			if (entity.Tags.Has<PlayerFaction>())
			{
				players.Add(Create(entity, RenderKind.Player, position));
			}
			else if (entity.Tags.Has<EnemyFaction>())
			{
				enemies.Add(Create(entity, RenderKind.Enemy, position));
			}
			else if (entity.Tags.Has<ProjectileTag>())
			{
				projectiles.Add(Create(entity, RenderKind.Projectile, position));
			}
		}

		players.Sort(CompareEntity);
		enemies.Sort(CompareEntity);
		projectiles.Sort(CompareEntity);

		MatchState match = world.GetState<MatchState>();
		return new ShooterFrameSnapshot(
			new RenderSnapshot(players.ToArray(), enemies.ToArray(), projectiles.ToArray()),
			new HudSnapshot(match.Score, match.AliveEnemies, match.Phase,
				completedTickIndex ?? world.TickIndex));
	}

	private static RenderEntitySnapshot Create(Entity entity, RenderKind kind, Position position) =>
		new(entity.Id, entity.Revision, kind, position.X, position.Z);

	private static int CompareEntity(RenderEntitySnapshot left, RenderEntitySnapshot right)
	{
		int byId = left.EntityId.CompareTo(right.EntityId);
		return byId != 0 ? byId : left.Revision.CompareTo(right.Revision);
	}
}

public sealed class ShooterSnapshotState
{
	public ShooterFrameSnapshot Current { get; set; } = ShooterFrameSnapshot.Empty;
}

internal sealed class ShooterSnapshotExtractSystem : EcsSystem
{
	private readonly ShooterSnapshotExtractor _extractor = new();

	protected override void Execute()
	{
		Res<ShooterSnapshotState>().Current = _extractor.Extract(World, World.TickIndex + 1);
	}
}
