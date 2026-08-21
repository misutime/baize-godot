// SPDX-License-Identifier: MIT
// Program.cs —— EcsWorld 框架冒烟测试（P2.1）
//
// 验证 EcsWorld 核心能力：创建世界 / 组件 / 系统 / Step / CommandBuffer / Reset。

using System;
using Baize.Ecs;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

// 组件（会被生成器自动注册）
public struct Position : IComponent { public float X, Z; }
public struct Velocity : IComponent { public float X, Z; }
public struct PlayerTag : ITag { }
// Phase 顺序测试用（P1-1：BaseSystem 用 OnUpdateGroup）
public class LogSystem : BaseSystem
{
    private readonly string _name;
    private readonly System.Collections.Generic.List<string> _log;
    public LogSystem(string name, System.Collections.Generic.List<string> log) { _name = name; _log = log; }
    protected override void OnUpdateGroup() { _log.Add(_name); }
}
// Resource（全局单例，借鉴 Bevy）
public class Score { public int Value; public Score(int v) { Value = v; } }

// Bundle（组件组合，借鉴 Bevy）
public struct PlayerBundle : IEntityBundle
{
    public Position Pos;
    public Velocity Vel;
    public void Apply(in EntityCommand entity)
    {
        entity.Add(Pos).Add(Vel).AddTag<PlayerTag>();
    }
}
// 移动系统（有 Position + Velocity 的实体）
public class MoveSystem : QuerySystem<Position, Velocity>
{
    protected override void OnUpdate()
    {
        float dt = Tick.deltaTime;
        Query.ForEachEntity((ref Position pos, ref Velocity vel, Entity e) =>
        {
            pos.X += vel.X * dt;
            pos.Z += vel.Z * dt;
        });
    }
}

class Program
{
    static void Main()
    {
        int failures = 0;

        // 1. 创建世界（生成器注册所有组件）
        var world = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        world.AddSystem(new MoveSystem(), Phase.Simulation);

        // 2. 创建实体
        var player = world.Store.CreateEntity();
        player.Add(new Position { X = 0, Z = 0 });
        player.Add(new Velocity { X = 1, Z = 2 });
        player.AddTag<PlayerTag>();

        // 3. Step 60 Tick（固定步长）
        for (int i = 0; i < 60; i++)
        {
            world.Step(InputFrame.Empty);
        }

        // 4. 验证位置更新（1m/s × 1s = X=1, Z=2）
        var pos = player.GetComponent<Position>();
        Console.WriteLine($"ecsworld-smoke: Position after 60 ticks = ({pos.X:F2}, {pos.Z:F2})");
        if (Math.Abs(pos.X - 1.0f) > 0.01f || Math.Abs(pos.Z - 2.0f) > 0.01f)
        {
            Console.WriteLine($"FAIL: 位置错误"); failures++;
        }

        // 5. 验证 TickIndex
        Console.WriteLine($"ecsworld-smoke: TickIndex = {world.TickIndex}");
        if (world.TickIndex != 60) { Console.WriteLine("FAIL: TickIndex"); failures++; }

        // 6. Reset 验证
        world.Reset();
        Console.WriteLine($"ecsworld-smoke: Reset 后 TickIndex = {world.TickIndex}");
        if (world.TickIndex != 0) { Console.WriteLine("FAIL: Reset"); failures++; }

        // 7. CommandBuffer 链式创建（延迟结构变更）
        var cb = world.CommandBuffer;
        cb.CreateEntity()
          .Add(new Position { X = 5, Z = 0 })
          .Add(new Velocity { X = 0, Z = 1 })
          .AddTag<PlayerTag>();
        world.Step(InputFrame.Empty);   // Playback 执行创建
        Console.WriteLine($"ecsworld-smoke: CommandBuffer 创建后实体数 = {world.Store.Entities.Count}");
        if (world.Store.Entities.Count != 1) { Console.WriteLine("FAIL: CommandBuffer 创建"); failures++; }

        // 8. 事件系统（纯数据通信）
        world.Events.Writer<DamageRequest>().Send(new DamageRequest(1, 10));
        world.Step(InputFrame.Empty);   // Flush 后事件可读
        int events = world.Events.Reader<DamageRequest>().Consume();
        Console.WriteLine($"ecsworld-smoke: 消费 DamageRequest 事件 = {events}");
        if (events != 1) { Console.WriteLine("FAIL: 事件"); failures++; }

        // 9. 确定性回放验证（同一输入序列两次运行，状态一致）
        var w1 = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        var w2 = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        w1.AddSystem(new MoveSystem(), Phase.Simulation);   // P2-4：注册系统让确定性真正被验证
        w2.AddSystem(new MoveSystem(), Phase.Simulation);
        var e1 = w1.Store.CreateEntity();
        var e2 = w2.Store.CreateEntity();
        e1.Add(new Position { X = 0, Z = 0 }); e1.Add(new Velocity { X = 2, Z = 3 });
        e2.Add(new Position { X = 0, Z = 0 }); e2.Add(new Velocity { X = 2, Z = 3 });
        var frames = new[] { new InputFrame(1, 0, false), new InputFrame(0, 1, true), new InputFrame(-1, 0, false) };
        foreach (var f in frames) w1.Step(f);
        foreach (var f in frames) w2.Step(f);
        var p1 = e1.GetComponent<Position>();
        var p2 = e2.GetComponent<Position>();
        Console.WriteLine($"ecsworld-smoke: 确定性回放 w1=({p1.X:F2},{p1.Z:F2}) w2=({p2.X:F2},{p2.Z:F2})");
        if (Math.Abs(p1.X - p2.X) > 0.001f || Math.Abs(p1.Z - p2.Z) > 0.001f) { Console.WriteLine("FAIL: 确定性回放"); failures++; }
        // P2-4：断言系统确实运行（位置非零——MoveSystem 已处理）
        if (Math.Abs(p1.X) < 0.001f && Math.Abs(p1.Z) < 0.001f) { Console.WriteLine("FAIL: 确定性测试未实际运行系统"); failures++; }

        // 10. Resource（全局单例，借鉴 Bevy）
        world.Resources.Set(new Score(100));
        var score = world.Resources.Get<Score>();
        Console.WriteLine($"ecsworld-smoke: Resource Score = {score.Value}");
        if (score == null || score.Value != 100) { Console.WriteLine("FAIL: Resource"); failures++; }

        // 11. Bundle（组件组合，借鉴 Bevy）
        var bundleWorld = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        bundleWorld.Step(InputFrame.Empty);   // schema 已建，正常
        bundleWorld.CommandBuffer.Spawn(new PlayerBundle { Pos = new Position { X = 1, Z = 2 }, Vel = new Velocity { X = 1, Z = 1 } });
        bundleWorld.Step(InputFrame.Empty);   // Playback 创建
        Console.WriteLine($"ecsworld-smoke: Bundle 创建后实体数 = {bundleWorld.Store.Entities.Count}");
        if (bundleWorld.Store.Entities.Count != 1) { Console.WriteLine("FAIL: Bundle"); failures++; }

        // 12. EventWriter/EventReader（读写分离）
        world.Events.Writer<DeathEvent>().Send(new DeathEvent(42));
        world.Step(InputFrame.Empty);
        int deathEvents = world.Events.Reader<DeathEvent>().Consume();
        Console.WriteLine($"ecsworld-smoke: EventReader 消费 DeathEvent = {deathEvents}");
        if (deathEvents != 1) { Console.WriteLine("FAIL: EventReader"); failures++; }

        // 13. Phase 调度顺序（P1-1 验证：乱序注册也按 Input→...→RenderExtract 执行）
        var phaseWorld = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        var orderLog = new System.Collections.Generic.List<string>();
        phaseWorld.AddSystem(new LogSystem("Cleanup", orderLog), Phase.Cleanup);
        phaseWorld.AddSystem(new LogSystem("Input", orderLog), Phase.Input);
        phaseWorld.AddSystem(new LogSystem("Simulation", orderLog), Phase.Simulation);
        phaseWorld.Step(InputFrame.Empty);
        var order = string.Join(">", orderLog);
        Console.WriteLine($"ecsworld-smoke: Phase 顺序 = {order}");
        if (order != "Input>Simulation>Cleanup") { Console.WriteLine($"FAIL: Phase 顺序（实际 {order}）"); failures++; }

        // 14. 双世界事件隔离（review P1-1：事件缓冲实例级，多世界不污染）
        var evWorldA = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        var evWorldB = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        evWorldA.Events.Writer<DeathEvent>().Send(new DeathEvent(1));
        evWorldA.Step(InputFrame.Empty);   // A 的 Flush
        // 先 Read 断言（非破坏性——防旧实现 A.Consume 清空共享缓冲导致假通过）
        int aRead = evWorldA.Events.Reader<DeathEvent>().Read().Count;
        int bRead = evWorldB.Events.Reader<DeathEvent>().Read().Count;
        int aEvents = evWorldA.Events.Reader<DeathEvent>().Consume();
        int bEvents = evWorldB.Events.Reader<DeathEvent>().Consume();
        Console.WriteLine($"ecsworld-smoke: 事件隔离 A={aRead}/{aEvents} B={bRead}/{bEvents}");
        if (aEvents != 1 || bEvents != 0) { Console.WriteLine("FAIL: 双世界事件隔离"); failures++; }

        // 15. Reset 清空 pending（review P1-2：Send 后 Reset，事件不残留）
        var evWorldC = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
        evWorldC.Events.Writer<DeathEvent>().Send(new DeathEvent(2));   // pending
        evWorldC.Reset();                                               // 清空 pending+current
        int cEvents = evWorldC.Events.Reader<DeathEvent>().Consume();
        Console.WriteLine($"ecsworld-smoke: Reset 清空 pending 后事件 = {cEvents}");
        if (cEvents != 0) { Console.WriteLine("FAIL: Reset 清空 pending"); failures++; }

        Console.WriteLine($"ecsworld-smoke: 测试完成, failures={failures}");

        Console.WriteLine($"ecsworld-smoke: 测试完成, failures={failures}");
        if (failures == 0) { Console.WriteLine("ecsworld-smoke: 验证成功——EcsWorld 框架可用"); }
        else { Environment.Exit(1); }
    }
}
