// SPDX-License-Identifier: MIT
// ShooterFeature.cs —— 玩法规则的系统注册表

using Baize.Ecs;

namespace ShooterPoc;

/// <summary>
/// Feature 只回答“启用哪些规则、因果顺序是什么”；它不创建实体，也不保存玩法数据。
/// </summary>
public sealed class ShooterFeature : IEcsFeature
{
	public void Install(EcsWorld world)
	{
		world.AddSystem(new ApplyPlayerInputSystem(world), Phase.Input);

		world.AddSystem(new FireWeaponSystem(world), Phase.Spawn);
		world.AddSystem(new SpawnEnemiesSystem(world), Phase.Spawn);

		world.AddSystem(new SeekPlayerSystem(world), Phase.Simulation);
		world.AddSystem(new MoveSystem(world), Phase.Simulation);

		world.AddSystem(new SweptProjectileHitSystem(world), Phase.Collision);
		world.AddSystem(new EnemyContactSystem(world), Phase.Collision);

		// 先结束对局并丢弃排队命令，再决定是否结算同 Tick 的伤害。
		world.AddSystem(new EndMatchSystem(world), Phase.Resolve);
		world.AddSystem(new ResolveDamageSystem(world), Phase.Resolve);

		world.AddSystem(new CleanupProjectilesSystem(world), Phase.Cleanup);
	}
}
