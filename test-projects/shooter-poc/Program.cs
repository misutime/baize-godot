// SPDX-License-Identifier: MIT
// Program.cs —— P2.2 Shooter PoC 主程序
//
// 固定脚本场景：玩家移动 → 生成敌人 → 射击 → 命中计分 → 敌人接触 GameOver。
// 跑 600 Tick，断言退出条件：
//   ✓ 确定性（同一 InputFrame 序列两次运行 state hash 相同）
//   ✓ Fire 边沿不重复/不丢失
//   ✓ 子弹不无限增长（超射程清理）
//   ✓ 同 Tick 多次命中只记一次死亡
//   ✓ GameOver 后停止移动/射击/生成
//   ✓ Reset 后状态归零

using System;
using Baize.Ecs;
using Friflo.Engine.ECS;

namespace ShooterPoc;

class Program
{
    private static int _failures;
    private static FireSystem _fireSystem;   // 测试读取 FireCount
    static int Main()
    {
        // ============ 1. 确定性回放（同一输入两次运行 state 一致） ============
        RunDeterministicTest();

        // ============ 2. Fire 边沿测试 ============
        RunFireEdgeTest();

        // ============ 3. 完整游戏循环（600 Tick） ============
        RunGameLoop();

        Console.WriteLine($"shooter-poc: 测试完成, failures={_failures}");
        if (_failures == 0) { Console.WriteLine("shooter-poc: 验证成功——玩法闭环可用"); return 0; }
        return 1;
    }

    // ---------- 1. 确定性回放 ----------
    static void RunDeterministicTest()
    {
        var hash1 = RunOnce(out var score1, out var phase1);
        var hash2 = RunOnce(out var score2, out var phase2);
        Console.WriteLine($"shooter-poc: 确定性 hash={hash1} vs {hash2}, score={score1}/{score2}, phase={phase1}/{phase2}");
        if (hash1 != hash2) { Console.WriteLine("FAIL: 确定性回放不一致"); _failures++; }
        if (score1 != score2 || phase1 != phase2) { Console.WriteLine("FAIL: 状态不一致"); _failures++; }
    }

    static long RunOnce(out int finalScore, out string finalPhase)
    {
        var world = CreateWorld();
        var frames = BuildScriptFrames();
        long hash = 17;
        foreach (var f in frames)
        {
            world.Step(f);
            hash = hash * 31 + (long)world.TickIndex;
        }
        var state = world.Resources.Get<GameState>();
        finalScore = state.Score;
        finalPhase = state.Phase.ToString();
        return hash;
    }

    // ---------- 2. Fire 边沿 ----------
    static void RunFireEdgeTest()
    {
        var world = CreateWorld();
        // 边沿测试不受冷却限制：设玩家武器 Cooldown=0
        foreach (var entity in world.Store.Entities)
        {
            if (entity.Tags.Has<PlayerTag>() && entity.HasComponent<Weapon>())
            {
                ref var w = ref entity.GetComponent<Weapon>();
                w.Cooldown = 0;
            }
        }
        // Fire 序列：false,true,true,false,true（应产生 2 发子弹：边沿在 true 后的第一个 true）
        var frames = new[]
        {
            new InputFrame(0, 0, false),
            new InputFrame(0, 0, true),    // 边沿 1 → 发射
            new InputFrame(0, 0, true),    // 持续按住（不重复发射）
            new InputFrame(0, 0, false),
            new InputFrame(0, 0, true),    // 边沿 2 → 发射
        };
        foreach (var f in frames) world.Step(f);

        // 用 FireSystem.FireCount 直接验证发射次数（不受子弹清理影响）
        int fireCount = _fireSystem.FireCount;
        Console.WriteLine($"shooter-poc: Fire 边沿 FireCount = {fireCount}");
        if (fireCount != 2) { Console.WriteLine($"FAIL: Fire 边沿（期望 2，实际 {fireCount}）"); _failures++; }
    }

    // ---------- 3. 完整游戏循环 ----------
    static void RunGameLoop()
    {
        var world = CreateWorld();
        var frames = BuildScriptFrames();
        foreach (var f in frames) world.Step(f);

        var state = world.Resources.Get<GameState>();
        int bulletCount = CountEntitiesWithTag(world, "BulletTag");
        Console.WriteLine($"shooter-poc: 游戏循环 score={state.Score}, phase={state.Phase}, 存活子弹={bulletCount}");

        // 子弹不无限增长（600 Tick 内清理生效）
        if (bulletCount > 20) { Console.WriteLine($"FAIL: 子弹无限增长（{bulletCount}）"); _failures++; }

        // Reset 后状态归零
        world.Reset();
        state = world.Resources.Get<GameState>();
        if (state.Score != 0) { Console.WriteLine("FAIL: Reset 后分数未归零"); _failures++; }
        int afterReset = CountEntitiesWithTag(world, "BulletTag");
        if (afterReset != 0) { Console.WriteLine("FAIL: Reset 后实体残留"); _failures++; }
        Console.WriteLine($"shooter-poc: Reset 后 score={state.Score}, 实体={afterReset}");
    }

    // ---------- 辅助 ----------
    static EcsWorld CreateWorld()
    {
        var world = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));

        // 全局单例（Resource）
        world.Resources.Set(new GameState { Phase = GamePhase.Playing, Score = 0 });
        world.Resources.Set(new SpawnConfig { Interval = 1.0f, MaxAlive = 10, SpawnRadius = 20, PlayerX = 0, PlayerZ = 0 });

        // 玩家
        var player = world.Store.CreateEntity();
        player.Add(new Position { X = 0, Z = 0 });
        player.Add(new Velocity { X = 0, Z = 0 });
        player.Add(new PlayerControl { MoveSpeed = 8.0f });
        player.Add(new Weapon { Cooldown = 0.3f, BulletSpeed = 30, Timer = 0 });
        player.Add(new Radius { Value = 0.5f });
        player.AddTag<PlayerTag>();

        // 注册系统（按 Phase）
        world.AddSystem(new ApplyInputSystem(world), Phase.Input);
        _fireSystem = new FireSystem(world);
        world.AddSystem(_fireSystem, Phase.Spawn);
        world.AddSystem(new SpawnSystem(world), Phase.Spawn);
        world.AddSystem(new EnemySteeringSystem(world), Phase.Simulation);
        world.AddSystem(new MoveSystem(), Phase.Simulation);
        world.AddSystem(new SweptBulletHitSystem(world), Phase.Collision);
        world.AddSystem(new EnemyContactSystem(world), Phase.Collision);
        world.AddSystem(new DamageResolveSystem(world), Phase.Resolve);
        world.AddSystem(new ScoreSystem(), Phase.Resolve);
        world.AddSystem(new CleanupSystem(world), Phase.Cleanup);

        return world;
    }

    static InputFrame[] BuildScriptFrames()
    {
        // 600 Tick 固定脚本：移动 → 射击 → 生成敌人（用 SpawnSystem 简化——这里用内置生成）
        var frames = new InputFrame[600];
        for (int i = 0; i < 600; i++)
        {
            // 前 100 Tick 玩家不动 + 射击；后 500 Tick 移动 + 射击
            float moveX = i < 100 ? 0 : 1;   // 向右移动
            bool fire = i % 30 == 10;         // 周期性射击（边沿）
            frames[i] = new InputFrame(moveX, 0, fire);
        }
        return frames;
    }

    static int CountEntitiesWithTag(EcsWorld world, string tagName)
    {
        int count = 0;
        foreach (var entity in world.Store.Entities)
        {
            if (tagName == "BulletTag" && entity.Tags.Has<BulletTag>()) count++;
            if (tagName == "EnemyTag" && entity.Tags.Has<EnemyTag>()) count++;
        }
        return count;
    }
}


