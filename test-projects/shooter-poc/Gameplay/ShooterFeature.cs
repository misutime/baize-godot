// SPDX-License-Identifier: MIT
// ShooterFeature.cs —— 通过嵌套 Feature 组合完整玩法规则

using Baize.Ecs;

namespace ShooterPoc;

/// <summary>大 Feature 只组合小 Feature；因果顺序由各 Phase 内的安装顺序表达。</summary>
public sealed class ShooterFeature : IEcsFeature
{
	public void Install(EcsWorld world)
	{
		world
			.AddFeature(new MatchFeature())
			.AddFeature(new CombatFeature())
			.AddFeature(new SpawningFeature())
			.AddFeature(new ActorsFeature())
			.AddFeature(new MovementFeature());
	}
}

internal sealed class MatchFeature : IEcsFeature
{
	public void Install(EcsWorld world)
	{
		// Resolve 内先结束对局，随后 CombatFeature 才会尝试结算伤害。
		world.AddSystem(new EndMatchSystem(), Phase.Resolve);
	}
}

internal sealed class CombatFeature : IEcsFeature
{
	public void Install(EcsWorld world)
	{
		world.AddSystem(new FireWeaponSystem(), Phase.Spawn);
		world.AddSystem(new SweptProjectileHitSystem(), Phase.Collision);
		world.AddSystem(new ResolveDamageSystem(), Phase.Resolve);
		world.AddSystem(new CleanupProjectilesSystem(), Phase.Cleanup);
	}
}

internal sealed class SpawningFeature : IEcsFeature
{
	public void Install(EcsWorld world) =>
		world.AddSystem(new SpawnEnemiesSystem(), Phase.Spawn);
}

internal sealed class ActorsFeature : IEcsFeature
{
	public void Install(EcsWorld world)
	{
		world.AddSystem(new ApplyPlayerInputSystem(), Phase.Input);
		world.AddSystem(new SeekPlayerSystem(), Phase.Simulation);
		world.AddSystem(new EnemyContactSystem(), Phase.Collision);
	}
}

internal sealed class MovementFeature : IEcsFeature
{
	public void Install(EcsWorld world) =>
		world.AddSystem(new MoveSystem(), Phase.Simulation);
}
