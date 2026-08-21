using System;
using Friflo.Engine.ECS;

// P2-1 验证：40 个组件类型（超过旧的 32 位 int 掩码限制）的关系/索引正常工作。
// 旧实现 isOwner/isLinked 是 int（32 位），超过 32 个类型的关系会截断失效。

// 40 个组件类型（索引 > 32 触发硬伤）
public struct C01 : IComponent { public int V; }
public struct C02 : IComponent { public int V; }
public struct C03 : IComponent { public int V; }
public struct C04 : IComponent { public int V; }
public struct C05 : IComponent { public int V; }
public struct C06 : IComponent { public int V; }
public struct C07 : IComponent { public int V; }
public struct C08 : IComponent { public int V; }
public struct C09 : IComponent { public int V; }
public struct C10 : IComponent { public int V; }
public struct C11 : IComponent { public int V; }
public struct C12 : IComponent { public int V; }
public struct C13 : IComponent { public int V; }
public struct C14 : IComponent { public int V; }
public struct C15 : IComponent { public int V; }
public struct C16 : IComponent { public int V; }
public struct C17 : IComponent { public int V; }
public struct C18 : IComponent { public int V; }
public struct C19 : IComponent { public int V; }
public struct C20 : IComponent { public int V; }
public struct C21 : IComponent { public int V; }
public struct C22 : IComponent { public int V; }
public struct C23 : IComponent { public int V; }
public struct C24 : IComponent { public int V; }
public struct C25 : IComponent { public int V; }
public struct C26 : IComponent { public int V; }
public struct C27 : IComponent { public int V; }
public struct C28 : IComponent { public int V; }
public struct C29 : IComponent { public int V; }
public struct C30 : IComponent { public int V; }
public struct C31 : IComponent { public int V; }
public struct C32 : IComponent { public int V; }
public struct C33 : IComponent { public int V; }
public struct C34 : IComponent { public int V; }
public struct C35 : IComponent { public int V; }
public struct C36 : IComponent { public int V; }
public struct C37 : IComponent { public int V; }
public struct C38 : IComponent { public int V; }
public struct C39 : IComponent { public int V; }
public struct C40 : IComponent { public int V; }

// 关系（IRelation<TKey>）——在 40 个组件之后注册，index 超过 32
public struct R1 : IRelation<long> { public long Key; public long GetRelationKey() => Key; }
public struct R2 : IRelation<long> { public long Key; public long GetRelationKey() => Key; }

// 索引组件（IIndexedComponent）——超过 32 的索引
public struct Ix33 : IIndexedComponent<int> { public int Value; public int GetIndexedValue() => Value; }
public struct Ix40 : IIndexedComponent<int> { public int Value; public int GetIndexedValue() => Value; }

class Program
{
    static void Main()
    {
        // P2-3：源生成器自动注册所有类型（替代手动 RegisterComponent/RegisterRelation）
        var aot = new NativeAOT();
        EcsAotRegistration.RegisterAll(aot);
        aot.CreateSchema();

        var store = new EntityStore();
        int failures = 0;

        // 1. 创建实体，加 C40 组件（第 40 个类型，远超 32 限制）
        var e1 = store.CreateEntity();
        e1.AddComponent(new C40 { V = 42 });
        if (e1.GetComponent<C40>().V != 42) { Console.WriteLine("FAIL: C40 组件存取"); failures++; }

        // 2. 关系 R2（第 42 个类型左右）——isOwner 掩码位超出旧 32 位 int
        var e2 = store.CreateEntity();
        e2.AddComponent(new C33 { V = 1 });  // 确保 e2 有组件
        e2.AddRelation(new R2 { Key = 100 });
        var r2 = e2.GetRelations<R2>();
        bool found = false;
        foreach (var r in r2) { if (r.Key == 100) found = true; }
        if (!found) { Console.WriteLine("FAIL: R2 关系未找到（掩码截断？）"); failures++; }

        // 3. 删除 e2 验证关系清理（isOwner Remove 路径）
        e2.DeleteEntity();

        // 4. 索引组件 Ix40（第 40+ 索引类型）
        var e3 = store.CreateEntity();
        e3.AddComponent(new Ix40 { Value = 7 });
        var query = store.Query<Ix40>();
        int count = 0;
        query.ForEachEntity((ref Ix40 ix, Entity e) => { if (ix.Value == 7) count++; });
        if (count != 1) { Console.WriteLine($"FAIL: Ix40 索引查询 count={count}"); failures++; }

        // 5. 关系 R1 + 索引 Ix33 组合（多掩码位）
        var e4 = store.CreateEntity();
        e4.AddComponent(new Ix33 { Value = 33 });
        e4.AddRelation(new R1 { Key = 1 });
        var r1 = e4.GetRelations<R1>();
        bool found1 = false;
        foreach (var r in r1) { if (r.Key == 1) found1 = true; }
        if (!found1) { Console.WriteLine("FAIL: R1 关系未找到"); failures++; }

        Console.WriteLine($"friflo-mask: 测试完成, failures={failures}");
        if (failures == 0) { Console.WriteLine("friflo-mask: 验证成功——40+ 类型关系/索引掩码已修复"); }
    }
}


