using System;
using Friflo.Engine.ECS;

// 组件定义
public struct Health : IComponent { public int Value; }
public struct Position : IComponent { public float X, Y; }

class Program
{
    static void Main()
    {
        var store = new EntityStore();

        // 创建 1 万实体
        for (int i = 0; i < 10000; i++)
        {
            var entity = store.CreateEntity();
            entity.AddComponent(new Health { Value = 100 });
            entity.AddComponent(new Position { X = i, Y = i * 2 });
        }

        // 查询所有有 Health 的实体并处理
        int count = 0;
        var query = store.Query<Health, Position>();
        query.ForEachEntity((ref Health health, ref Position pos, Entity entity) =>
        {
            health.Value -= 1;
            count++;
        });

        // 验证
        Console.WriteLine($"friflo-smoke: {count} 实体处理成功 (net11)");
        if (count == 10000) { Console.WriteLine("friflo-smoke: 验证成功"); }
        else { Environment.Exit(1); }  // FORK-CUSTOM：失败时非零退出（CI 门禁）
    }
}
