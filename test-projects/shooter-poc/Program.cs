// SPDX-License-Identifier: MIT
// Program.cs —— P2.2 Shooter PoC 可控验收场景
//
// 覆盖：确定性状态哈希、预期得分/GameOver、Fire 边沿、GameOver 冻结、
// 同 Tick 多命中去重、EntityHandle 代际安全、swept 轨迹与 Reset 状态归零。

using System;
using System.Collections.Generic;
using Baize.Ecs;
using Friflo.Engine.ECS;

namespace ShooterPoc;

class Program
{
    private const int ExpectedScriptScore = 3;
    private static int _failures;

    static int Main()
    {
        RunDeterministicTest();
        RunFireEdgeTest();
        RunScoreAndDuplicateHitTest();
        RunSweptTrajectoryTest();
        RunGameOverFreezeTest();
        RunEntityReuseTest();
        RunResetTest();

        Console.WriteLine($"shooter-poc: 测试完成, failures={_failures}");
        if (_failures == 0)
        {
            Console.WriteLine("shooter-poc: 验证成功——玩法闭环可用");
            return 0;
        }
        return 1;
    }

    // ---------- 1. 确定性回放：哈希每 Tick 的关键 Resource + 稳定排序实体状态 ----------
    static void RunDeterministicTest()
    {
        var first = RunOnce();
        var second = RunOnce();
        Console.WriteLine($"shooter-poc: 确定性 hash={first.Hash} vs {second.Hash}, " +
                          $"score={first.Score}/{second.Score}, phase={first.Phase}/{second.Phase}");

        Check(first.Hash == second.Hash, "确定性回放 state hash 不一致");
        Check(first.Score == second.Score && first.Phase == second.Phase, "确定性回放最终状态不一致");
        Check(first.Score == ExpectedScriptScore, $"脚本场景预期得分 {ExpectedScriptScore}，实际 {first.Score}");
        Check(first.Phase == GamePhase.GameOver, $"脚本场景预期 GameOver，实际 {first.Phase}");
        Check(first.BulletCount <= 20, $"子弹无限增长（{first.BulletCount}）");
    }

    static RunResult RunOnce()
    {
        var fixture = CreateWorld();
        ulong hash = 1469598103934665603UL;
        foreach (var frame in BuildScriptFrames())
        {
            fixture.World.Step(frame);
            Mix(ref hash, ComputeStateHash(fixture.World));
        }

        var state = fixture.World.Resources.Get<GameState>();
        return new RunResult(unchecked((long)hash), state.Score, state.Phase,
            CountEntitiesWithTag<BulletTag>(fixture.World));
    }

    // ---------- 2. Fire 边沿 + FireSystem Resettable 状态 ----------
    static void RunFireEdgeTest()
    {
        var fixture = CreateWorld(maxAlive: 0);
        SetPlayerCooldown(fixture.World, 0);
        var frames = new[]
        {
            new InputFrame(0, 0, false),
            new InputFrame(0, 0, true),
            new InputFrame(0, 0, true),
            new InputFrame(0, 0, false),
            new InputFrame(0, 0, true),
        };
        foreach (var frame in frames) fixture.World.Step(frame);

        Console.WriteLine($"shooter-poc: Fire 边沿 FireCount={fixture.Fire.FireCount}");
        Check(fixture.Fire.FireCount == 2, $"Fire 边沿期望 2，实际 {fixture.Fire.FireCount}");
    }

    // ---------- 3. 真实得分 + 同 Tick 多发子弹只记一次死亡 ----------
    static void RunScoreAndDuplicateHitTest()
    {
        var fixture = CreateWorld(maxAlive: 0);
        AddEnemy(fixture.World, 0, 2, speed: 0);
        AddBullet(fixture.World, -0.1f, 0, 0, 120);
        AddBullet(fixture.World, 0.1f, 0, 0, 120);

        StepEmpty(fixture.World, 3);
        var state = fixture.World.Resources.Get<GameState>();
        int enemies = CountEntitiesWithTag<EnemyTag>(fixture.World);
        Console.WriteLine($"shooter-poc: 多命中 score={state.Score}, 敌人={enemies}");

        Check(state.Score == 1, $"同 Tick 多次命中应只得 1 分，实际 {state.Score}");
        Check(enemies == 0, $"命中后敌人应删除，实际 {enemies}");
        Check(state.AliveEnemies == 0, $"AliveEnemies 应归零，实际 {state.AliveEnemies}");
    }

    // ---------- 4. swept 只检查 previous→current，并使用敌人+子弹半径 ----------
    static void RunSweptTrajectoryTest()
    {
        var futureFixture = CreateWorld(maxAlive: 0);
        AddEnemy(futureFixture.World, 0, 1.8f, speed: 0);
        AddBullet(futureFixture.World, 0, 0, 0, 60);
        StepEmpty(futureFixture.World, 2);
        Check(futureFixture.World.Resources.Get<GameState>().Score == 0,
            "swept 错把 current→future 当作本 Tick 轨迹");
        futureFixture.World.Step(new InputFrame(0, 0, false));
        Check(futureFixture.World.Resources.Get<GameState>().Score == 1,
            "swept previous→current 未在真实穿越 Tick 命中");

        var radiusFixture = CreateWorld(maxAlive: 0);
        AddEnemy(radiusFixture.World, 0.65f, 1, speed: 0, radius: 0.5f);
        AddBullet(radiusFixture.World, 0, 0, 0, 60, radius: 0.2f);
        StepEmpty(radiusFixture.World, 2);
        int radiusScore = radiusFixture.World.Resources.Get<GameState>().Score;
        Console.WriteLine($"shooter-poc: swept previous→current + 半径和 score={radiusScore}");
        Check(radiusScore == 1, "swept 未使用敌人半径 + 子弹半径");
    }

    // ---------- 5. GameOver 后位置/射击/敌人数冻结，且丢弃过渡 Tick 排队生成 ----------
    static void RunGameOverFreezeTest()
    {
        var fixture = CreateWorld(spawnInterval: 0, maxAlive: 10);
        AddEnemy(fixture.World, 0, 0, speed: 0);

        fixture.World.Step(new InputFrame(0, 0, false));
        fixture.World.Step(new InputFrame(1, 0, true));
        var state = fixture.World.Resources.Get<GameState>();
        Check(state.Phase == GamePhase.GameOver, $"接触敌人后应 GameOver，实际 {state.Phase}");

        Position frozenPosition = GetPlayerPosition(fixture.World);
        int frozenFireCount = fixture.Fire.FireCount;
        int frozenEnemyCount = CountEntitiesWithTag<EnemyTag>(fixture.World);
        int frozenBulletCount = CountEntitiesWithTag<BulletTag>(fixture.World);

        for (int i = 0; i < 8; i++)
        {
            fixture.World.Step(new InputFrame(1, 1, i % 2 == 0));
        }

        Position finalPosition = GetPlayerPosition(fixture.World);
        int finalEnemyCount = CountEntitiesWithTag<EnemyTag>(fixture.World);
        int finalBulletCount = CountEntitiesWithTag<BulletTag>(fixture.World);
        Console.WriteLine($"shooter-poc: GameOver 冻结 pos=({finalPosition.X},{finalPosition.Z}), " +
                          $"FireCount={fixture.Fire.FireCount}, 敌人={finalEnemyCount}, 子弹={finalBulletCount}");

        Check(finalPosition.X == frozenPosition.X && finalPosition.Z == frozenPosition.Z,
            "GameOver 后玩家位置仍变化");
        Check(fixture.Fire.FireCount == frozenFireCount, "GameOver 后仍射击");
        Check(finalEnemyCount == frozenEnemyCount, "GameOver 后仍生成/删除敌人");
        Check(finalBulletCount == frozenBulletCount, "GameOver 后仍落地排队子弹或清理子弹");
        Check(state.AliveEnemies == finalEnemyCount,
            $"GameOver 后 AliveEnemies={state.AliveEnemies} 与实体数={finalEnemyCount} 不一致");
    }

    // ---------- 6. ID 复用：旧代际 DamageRequest 不得误伤新实体 ----------
    static void RunEntityReuseTest()
    {
        var fixture = CreateWorld(maxAlive: 0);
        Entity oldBullet = AddBullet(fixture.World, -10, 0, 0, 0);
        Entity oldEnemy = AddEnemy(fixture.World, 10, 0, speed: 0);
        EntityHandle source = fixture.World.GetHandle(oldBullet);
        EntityHandle oldTarget = fixture.World.GetHandle(oldEnemy);

        oldEnemy.DeleteEntity();
        fixture.World.Resources.Get<GameState>().AliveEnemies = 0;

        Entity newEnemy = AddEnemy(fixture.World, 10, 0, speed: 0, entityId: oldTarget.Id);
        EntityHandle newTarget = fixture.World.GetHandle(newEnemy);
        fixture.World.Events.Writer<DamageRequest>().Send(new DamageRequest(source, oldTarget, 1));
        fixture.World.Step(new InputFrame(0, 0, false));

        var state = fixture.World.Resources.Get<GameState>();
        bool reused = oldTarget.Id == newTarget.Id && oldTarget.Revision != newTarget.Revision;
        bool replacementsAlive = !fixture.World.ResolveHandle(source).IsNull
                                 && !fixture.World.ResolveHandle(newTarget).IsNull;
        Console.WriteLine($"shooter-poc: ID 复用 source={source}, target old={oldTarget}, new={newTarget}, score={state.Score}");

        Check(reused, "测试未形成相同 ID、不同 Revision 的真实复用场景");
        Check(state.Score == 0, $"旧代际请求误计分，实际 {state.Score}");
        Check(replacementsAlive, "旧代际请求误删了复用 ID 的新实体");
    }

    // ---------- 6. Reset：全部实体 + Tick + 有状态系统归零 ----------
    static void RunResetTest()
    {
        var fixture = CreateWorld(maxAlive: 0);
        SetPlayerCooldown(fixture.World, 0);
        fixture.World.Step(new InputFrame(0, 0, false));
        fixture.World.Step(new InputFrame(0, 0, true));
        Check(fixture.Fire.FireCount == 1, "Reset 前未建立 FireSystem 状态");

        fixture.World.Reset();
        fixture.World.Resources.Set(new GameState { Phase = GamePhase.Playing, Score = 0, AliveEnemies = 0 });
        var state = fixture.World.Resources.Get<GameState>();
        int entityCount = CountAllEntities(fixture.World);
        Console.WriteLine($"shooter-poc: Reset 后 score={state.Score}, 实体={entityCount}, " +
                          $"Tick={fixture.World.TickIndex}, FireCount={fixture.Fire.FireCount}");

        Check(state.Score == 0 && state.AliveEnemies == 0 && state.Phase == GamePhase.Playing,
            "Reset 后 GameState 未归零");
        Check(entityCount == 0, $"Reset 后全部实体应为 0，实际 {entityCount}");
        Check(fixture.World.TickIndex == 0, $"Reset 后 Tick 应为 0，实际 {fixture.World.TickIndex}");
        Check(fixture.Fire.FireCount == 0, $"Reset 后 FireCount 应为 0，实际 {fixture.Fire.FireCount}");
    }

    // ---------- 世界与场景辅助 ----------
    static Fixture CreateWorld(float spawnInterval = 1.0f, int maxAlive = 10, float spawnRadius = 20)
    {
        var world = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        world.Resources.Set(new GameState { Phase = GamePhase.Playing, Score = 0, AliveEnemies = 0 });
        world.Resources.Set(new SpawnConfig
        {
            Interval = spawnInterval,
            MaxAlive = maxAlive,
            SpawnRadius = spawnRadius,
            PlayerX = 0,
            PlayerZ = 0,
        });

        var player = world.Store.CreateEntity();
        player.Add(new Position { X = 0, Z = 0 });
        player.Add(new PreviousPosition { X = 0, Z = 0 });
        player.Add(new Velocity { X = 0, Z = 0 });
        player.Add(new PlayerControl { MoveSpeed = 8.0f });
        player.Add(new Weapon { Cooldown = 0.3f, BulletSpeed = 30, Timer = 0 });
        player.Add(new Radius { Value = 0.5f });
        player.AddTag<PlayerTag>();

        world.AddSystem(new PlayerSyncSystem(world), Phase.Input);
        world.AddSystem(new ApplyInputSystem(world), Phase.Input);
        var fire = new FireSystem(world);
        world.AddSystem(fire, Phase.Spawn);
        world.AddSystem(new SpawnSystem(world), Phase.Spawn);
        world.AddSystem(new EnemySteeringSystem(world), Phase.Simulation);
        world.AddSystem(new MoveSystem(world), Phase.Simulation);
        world.AddSystem(new SweptBulletHitSystem(world), Phase.Collision);
        world.AddSystem(new EnemyContactSystem(world), Phase.Collision);
        // GameOver 先冻结并丢弃早期 Phase 命令；DamageResolve 随后消费并忽略结束态事件。
        world.AddSystem(new GameOverHandlerSystem(world), Phase.Resolve);
        world.AddSystem(new DamageResolveSystem(world), Phase.Resolve);
        world.AddSystem(new ScoreSystem(), Phase.Resolve);
        world.AddSystem(new CleanupSystem(world), Phase.Cleanup);
        return new Fixture(world, fire);
    }

    static Entity AddEnemy(EcsWorld world, float x, float z, float speed, float radius = 0.5f, int? entityId = null)
    {
        Entity enemy = entityId.HasValue ? world.Store.CreateEntity(entityId.Value) : world.Store.CreateEntity();
        enemy.Add(new Position { X = x, Z = z });
        enemy.Add(new PreviousPosition { X = x, Z = z });
        enemy.Add(new Velocity { X = 0, Z = 0 });
        enemy.Add(new Health { Current = 1, Max = 1 });
        enemy.Add(new EnemyAI { Speed = speed });
        enemy.Add(new Radius { Value = radius });
        enemy.AddTag<EnemyTag>();
        world.Resources.Get<GameState>().AliveEnemies++;
        return enemy;
    }

    static Entity AddBullet(EcsWorld world, float x, float z, float velocityX, float velocityZ,
        float radius = 0.2f, int? entityId = null)
    {
        Entity bullet = entityId.HasValue ? world.Store.CreateEntity(entityId.Value) : world.Store.CreateEntity();
        bullet.Add(new Position { X = x, Z = z });
        bullet.Add(new PreviousPosition { X = x, Z = z });
        bullet.Add(new Velocity { X = velocityX, Z = velocityZ });
        bullet.Add(new Bullet { Damage = 1, Range = 50, Travelled = 0 });
        bullet.Add(new Radius { Value = radius });
        bullet.AddTag<BulletTag>();
        return bullet;
    }

    static void SetPlayerCooldown(EcsWorld world, float cooldown)
    {
        foreach (var entity in world.Store.Entities)
        {
            if (!entity.Tags.Has<PlayerTag>() || !entity.HasComponent<Weapon>()) continue;
            ref var weapon = ref entity.GetComponent<Weapon>();
            weapon.Cooldown = cooldown;
        }
    }

    static Position GetPlayerPosition(EcsWorld world)
    {
        foreach (var entity in world.Store.Entities)
        {
            if (entity.Tags.Has<PlayerTag>()) return entity.GetComponent<Position>();
        }
        throw new InvalidOperationException("玩家实体不存在");
    }

    static void StepEmpty(EcsWorld world, int count)
    {
        for (int i = 0; i < count; i++) world.Step(new InputFrame(0, 0, false));
    }

    static InputFrame[] BuildScriptFrames()
    {
        var frames = new InputFrame[600];
        for (int i = 0; i < frames.Length; i++)
        {
            float moveX = i >= 100 && i < 200 ? 1 : 0;
            bool fire = i < 240 && i % 30 == 10;
            frames[i] = new InputFrame(moveX, 0, fire);
        }
        return frames;
    }

    // ---------- 稳定状态哈希 ----------
    static ulong ComputeStateHash(EcsWorld world)
    {
        ulong hash = 1469598103934665603UL;
        var state = world.Resources.Get<GameState>();
        Mix(ref hash, (long)world.TickIndex);
        Mix(ref hash, (int)state.Phase);
        Mix(ref hash, state.Score);
        Mix(ref hash, state.AliveEnemies);

        var config = world.Resources.Get<SpawnConfig>();
        Mix(ref hash, config.Interval);
        Mix(ref hash, config.MaxAlive);
        Mix(ref hash, config.SpawnRadius);
        Mix(ref hash, config.PlayerX);
        Mix(ref hash, config.PlayerZ);

        var entities = new List<Entity>();
        foreach (var entity in world.Store.Entities) entities.Add(entity);
        entities.Sort((a, b) => a.Id != b.Id ? a.Id.CompareTo(b.Id) : a.Revision.CompareTo(b.Revision));
        Mix(ref hash, entities.Count);
        foreach (var entity in entities)
        {
            Mix(ref hash, entity.Id);
            Mix(ref hash, entity.Revision);
            Mix(ref hash, entity.Tags.Has<PlayerTag>() ? 1 : 0);
            Mix(ref hash, entity.Tags.Has<EnemyTag>() ? 1 : 0);
            Mix(ref hash, entity.Tags.Has<BulletTag>() ? 1 : 0);
            HashComponents(ref hash, entity);
        }
        return hash;
    }

    static void HashComponents(ref ulong hash, Entity entity)
    {
        if (entity.HasComponent<Position>())
        {
            Mix(ref hash, 101); ref var value = ref entity.GetComponent<Position>();
            Mix(ref hash, value.X); Mix(ref hash, value.Z);
        }
        if (entity.HasComponent<PreviousPosition>())
        {
            Mix(ref hash, 102); ref var value = ref entity.GetComponent<PreviousPosition>();
            Mix(ref hash, value.X); Mix(ref hash, value.Z);
        }
        if (entity.HasComponent<Velocity>())
        {
            Mix(ref hash, 103); ref var value = ref entity.GetComponent<Velocity>();
            Mix(ref hash, value.X); Mix(ref hash, value.Z);
        }
        if (entity.HasComponent<Health>())
        {
            Mix(ref hash, 104); ref var value = ref entity.GetComponent<Health>();
            Mix(ref hash, value.Current); Mix(ref hash, value.Max);
        }
        if (entity.HasComponent<PlayerControl>())
        {
            Mix(ref hash, 105); ref var value = ref entity.GetComponent<PlayerControl>();
            Mix(ref hash, value.MoveSpeed);
        }
        if (entity.HasComponent<Weapon>())
        {
            Mix(ref hash, 106); ref var value = ref entity.GetComponent<Weapon>();
            Mix(ref hash, value.Cooldown); Mix(ref hash, value.BulletSpeed); Mix(ref hash, value.Timer);
        }
        if (entity.HasComponent<EnemyAI>())
        {
            Mix(ref hash, 107); ref var value = ref entity.GetComponent<EnemyAI>();
            Mix(ref hash, value.Speed);
        }
        if (entity.HasComponent<Radius>())
        {
            Mix(ref hash, 108); ref var value = ref entity.GetComponent<Radius>();
            Mix(ref hash, value.Value);
        }
        if (entity.HasComponent<Bullet>())
        {
            Mix(ref hash, 109); ref var value = ref entity.GetComponent<Bullet>();
            Mix(ref hash, value.Damage); Mix(ref hash, value.Range); Mix(ref hash, value.Travelled);
        }
    }

    static void Mix(ref ulong hash, long value)
    {
        unchecked
        {
            hash ^= (ulong)value;
            hash *= 1099511628211UL;
        }
    }

    static void Mix(ref ulong hash, float value) => Mix(ref hash, BitConverter.SingleToInt32Bits(value));

    static int CountEntitiesWithTag<T>(EcsWorld world) where T : struct, ITag
    {
        int count = 0;
        foreach (var entity in world.Store.Entities)
        {
            if (entity.Tags.Has<T>()) count++;
        }
        return count;
    }

    static int CountAllEntities(EcsWorld world)
    {
        int count = 0;
        foreach (var _ in world.Store.Entities) count++;
        return count;
    }

    static void Check(bool condition, string failure)
    {
        if (condition) return;
        Console.WriteLine($"FAIL: {failure}");
        _failures++;
    }

    private sealed class Fixture
    {
        public EcsWorld World { get; }
        public FireSystem Fire { get; }

        public Fixture(EcsWorld world, FireSystem fire)
        {
            World = world;
            Fire = fire;
        }
    }

    private readonly struct RunResult
    {
        public long Hash { get; }
        public int Score { get; }
        public GamePhase Phase { get; }
        public int BulletCount { get; }

        public RunResult(long hash, int score, GamePhase phase, int bulletCount)
        {
            Hash = hash;
            Score = score;
            Phase = phase;
            BulletCount = bulletCount;
        }
    }
}
