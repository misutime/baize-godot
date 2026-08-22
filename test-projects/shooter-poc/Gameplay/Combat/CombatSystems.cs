// SPDX-License-Identifier: MIT
// CombatSystems.cs —— 射击、命中、伤害结算与投射物生命周期

using System;
using System.Collections.Generic;
using Baize.Ecs;
using Friflo.Engine.ECS;

namespace ShooterPoc;

public sealed class FireWeaponSystem : EcsSystem<Position, WeaponConfig, Cooldown>
{
	public FireWeaponSystem()
	{
		RunInState<MatchState>(GamePhase.Playing);
		Filter.AllTags(Tags.Get<PlayerFaction>());
	}

	protected override void Execute()
	{
		FireInputState edgeState = Res<FireInputState>();
		bool firePressed = Input.FirePressed;
		bool fireEdge = firePressed && !edgeState.WasPressed;
		float delta = Tick.deltaTime;

		Query.ForEachEntity((ref Position position, ref WeaponConfig weapon,
			ref Cooldown cooldown, Entity player) =>
		{
			cooldown.Remaining -= delta;
			if (!fireEdge || cooldown.Remaining > 0) return;

			cooldown.Remaining = weapon.CooldownSeconds;
			World.CommandBuffer.Spawn(new ProjectileBundle(
				position.X, position.Z, 0, weapon.ProjectileSpeed));
		});

		edgeState.WasPressed = firePressed;
	}
}

public sealed class SweptProjectileHitSystem
	: EcsSystem<Position, PreviousPosition, ProjectileConfig, CollisionRadius>
{
	public SweptProjectileHitSystem()
	{
		RunInState<MatchState>(GamePhase.Playing);
		Filter.AllTags(Tags.Get<ProjectileTag>());
	}

	protected override void Execute()
	{
		EventWriter<DamageRequested> writer = WriteEvents<DamageRequested>();
		Query.ForEachEntity((ref Position position, ref PreviousPosition previous,
			ref ProjectileConfig projectile, ref CollisionRadius projectileRadius, Entity source) =>
		{
			foreach (var target in World.Store.Query<Position, Health, CollisionRadius>()
						 .AllTags(Tags.Get<EnemyFaction>()).Entities)
			{
				ref Position targetPosition = ref target.GetComponent<Position>();
				ref CollisionRadius targetRadius = ref target.GetComponent<CollisionRadius>();
				float combinedRadius = projectileRadius.Value + targetRadius.Value;
				float distance = SegmentPointDistance(
					previous.X, previous.Z, position.X, position.Z,
					targetPosition.X, targetPosition.Z);
				if (distance > combinedRadius) continue;

				writer.Send(new DamageRequested(
					World.GetHandle(source), World.GetHandle(target), projectile.Damage));
				break;
			}
		});
	}

	private static float SegmentPointDistance(
		float x1, float z1, float x2, float z2, float pointX, float pointZ)
	{
		float dx = x2 - x1;
		float dz = z2 - z1;
		float lengthSquared = dx * dx + dz * dz;
		if (lengthSquared < 0.0001f)
		{
			float pointDx = pointX - x1;
			float pointDz = pointZ - z1;
			return MathF.Sqrt(pointDx * pointDx + pointDz * pointDz);
		}

		float projection = ((pointX - x1) * dx + (pointZ - z1) * dz) / lengthSquared;
		projection = MathF.Max(0, MathF.Min(1, projection));
		float closestX = x1 + projection * dx;
		float closestZ = z1 + projection * dz;
		float closestDx = pointX - closestX;
		float closestDz = pointZ - closestZ;
		return MathF.Sqrt(closestDx * closestDx + closestDz * closestDz);
	}
}

public sealed class ResolveDamageSystem : EcsSystem
{
	// 每次 Execute 开头清空：仅是本 Tick 去重工作区，不是玩法状态。
	private readonly HashSet<EntityHandle> _hitTargets = new();
	private readonly HashSet<EntityHandle> _hitSources = new();

	public ResolveDamageSystem() => RunInState<MatchState>(GamePhase.Playing);

	protected override void Execute()
	{
		EventReader<DamageRequested> reader = ReadEvents<DamageRequested>();
		MatchState match = State<MatchState>();
		_hitTargets.Clear();
		_hitSources.Clear();
		foreach (DamageRequested request in reader.Read())
		{
			Entity source = World.ResolveHandle(request.Source);
			Entity target = World.ResolveHandle(request.Target);
			if (source.IsNull || target.IsNull
				|| !source.Tags.Has<ProjectileTag>()
				|| !target.Tags.Has<EnemyFaction>()
				|| !target.HasComponent<Health>())
			{
				continue;
			}

			if (!_hitSources.Add(request.Source)) continue;
			World.CommandBuffer.DeleteEntity(source.Id);

			// 同 Tick 同一目标只结算一次，避免多个投射物重复消费同一次死亡。
			if (!_hitTargets.Add(request.Target)) continue;

			ref Health health = ref target.GetComponent<Health>();
			health.Current -= request.Amount;
			if (health.Current > 0) continue;

			World.CommandBuffer.DeleteEntity(target.Id);
			match.Score++;
			if (match.AliveEnemies > 0) match.AliveEnemies--;
		}

		reader.Consume();
	}
}

public sealed class CleanupProjectilesSystem
	: EcsSystem<Velocity, TravelDistance, ProjectileConfig>
{
	public CleanupProjectilesSystem()
	{
		RunInState<MatchState>(GamePhase.Playing);
		Filter.AllTags(Tags.Get<ProjectileTag>());
	}

	protected override void Execute()
	{
		float delta = Tick.deltaTime;
		Query.ForEachEntity((ref Velocity velocity, ref TravelDistance travelled,
			ref ProjectileConfig config, Entity entity) =>
		{
			travelled.Value += MathF.Sqrt(
				velocity.X * velocity.X + velocity.Z * velocity.Z) * delta;
			if (travelled.Value > config.MaxRange)
			{
				World.CommandBuffer.DeleteEntity(entity.Id);
			}
		});
	}
}
