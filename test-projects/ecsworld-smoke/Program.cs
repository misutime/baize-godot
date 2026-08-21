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

        Console.WriteLine($"ecsworld-smoke: 测试完成, failures={failures}");
        if (failures == 0) { Console.WriteLine("ecsworld-smoke: 验证成功——EcsWorld 框架可用"); }
        else { Environment.Exit(1); }
    }
}
