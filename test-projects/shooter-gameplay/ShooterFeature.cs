// SPDX-License-Identifier: MIT
// ShooterFeature.cs —— 通过嵌套 Feature 组合完整玩法规则

using Sola3d.Ecs;

namespace Shooter.Gameplay;

/// <summary>大 Feature 只组合小 Feature；跨 Feature 因果顺序由 manifest 词法顺序表达。</summary>
[EcsFeature]
[AddFeature<MatchFeature>]
[AddFeature<CombatFeature>]
[AddFeature<SpawningFeature>]
[AddFeature<ActorsFeature>]
[AddFeature<MovementFeature>]
[AddFeature<SnapshotFeature>]
public sealed partial class ShooterFeature : IEcsFeature { }

/// <summary>Resolve 内先结束对局，随后 CombatFeature 才会尝试结算伤害。</summary>
[EcsFeature]
[AddSystem<EndMatchSystem>(Phase.Resolve)]
internal sealed partial class MatchFeature : IEcsFeature { }

[EcsFeature]
[AddSystem<FireWeaponSystem>(Phase.Spawn)]
[AddSystem<SweptProjectileHitSystem>(Phase.Collision)]
[AddSystem<ResolveDamageSystem>(Phase.Resolve)]
[AddSystem<CleanupProjectilesSystem>(Phase.Cleanup)]
internal sealed partial class CombatFeature : IEcsFeature { }

[EcsFeature]
[AddSystem<SpawnEnemiesSystem>(Phase.Spawn)]
internal sealed partial class SpawningFeature : IEcsFeature { }

[EcsFeature]
[AddSystem<ApplyPlayerInputSystem>(Phase.Input)]
[AddSystem<SeekPlayerSystem>(Phase.Simulation)]
[AddSystem<EnemyContactSystem>(Phase.Collision)]
internal sealed partial class ActorsFeature : IEcsFeature { }

[EcsFeature]
[AddSystem<MoveSystem>(Phase.Simulation)]
internal sealed partial class MovementFeature : IEcsFeature { }

[EcsFeature]
[AddSystem<ShooterSnapshotExtractSystem>(Phase.RenderExtract)]
internal sealed partial class SnapshotFeature : IEcsFeature { }
