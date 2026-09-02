using System;
using Friflo.Engine.ECS;

// P2-1 验证：63 个索引组件将后续索引类型推到第 64 位以上，覆盖完整 BitSet 宽度。
// 旧实现 isOwner/isLinked 是 int，批量/复制索引路径又只读取 l0，超过 32/64 位会截断失效。

// C01-C63 是索引组件，C64-C70 是普通组件。
public struct C01 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C02 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C03 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C04 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C05 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C06 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C07 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C08 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C09 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C10 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C11 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C12 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C13 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C14 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C15 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C16 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C17 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C18 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C19 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C20 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C21 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C22 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C23 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C24 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C25 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C26 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C27 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C28 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C29 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C30 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C31 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C32 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C33 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C34 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C35 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C36 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C37 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C38 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C39 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C40 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C41 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C42 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C43 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C44 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C45 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C46 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C47 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C48 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C49 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C50 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C51 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C52 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C53 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C54 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C55 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C56 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C57 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C58 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C59 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C60 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C61 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C62 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C63 : IIndexedComponent<int> { public int V; public int GetIndexedValue() => V; }
public struct C64 : IComponent { public int V; }
public struct C65 : IComponent { public int V; }
public struct C66 : IComponent { public int V; }
public struct C67 : IComponent { public int V; }
public struct C68 : IComponent { public int V; }
public struct C69 : IComponent { public int V; }
public struct C70 : IComponent { public int V; }

// 关系（IRelation<TKey>）——索引位超过 64
public struct R1 : IRelation<long> { public long Key; public long GetRelationKey() => Key; }
public struct R2 : IRelation<long> { public long Key; public long GetRelationKey() => Key; }

// 索引组件（IIndexedComponent）——StructIndex 超过 64
public struct Ix33 : IIndexedComponent<int> { public int Value; public int GetIndexedValue() => Value; }
public struct Ix40 : IIndexedComponent<int> { public int Value; public int GetIndexedValue() => Value; }

// P1-1 / P2-3：索引类型位于 64 位以上，并覆盖 struct/class/Entity 键、int 关系、record 和间接 Script。
public struct IxHigh : IIndexedComponent<int> { public int Value; public int GetIndexedValue() => Value; }
public struct IxString : IIndexedComponent<string> { public string Value; public string GetIndexedValue() => Value; }
public struct LinkHigh : ILinkComponent { public Entity Target; public Entity GetIndexedValue() => Target; }
public struct RInt : IRelation<int> { public int Key; public int GetRelationKey() => Key; }
public readonly record struct RecordC(int Value) : IComponent;
public abstract class IntermediateScript : Script { }
public sealed class ConcreteScript : IntermediateScript { public ConcreteScript() { } }

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
        int highIndex = EntityStore.GetEntitySchema().ComponentTypeByType[typeof(IxHigh)].StructIndex;
        if (highIndex <= 64) {
            Console.WriteLine($"FAIL: IxHigh StructIndex={highIndex}，未覆盖 >64 位索引"); failures++;
        }
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

        // 4. 索引组件 Ix40（实际 StructIndex > 64）
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

        // 6. >64 位索引：泛型创建路径
        var genericCreated = store.CreateEntity(new IxHigh { Value = 701 });
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(701).Count != 1) {
            Console.WriteLine("FAIL: >64 索引泛型创建路径"); failures++;
        }

        // 7. >64 位索引：CreateEntityBatch 路径
        var createBatchEntity = store.Batch().Add(new IxHigh { Value = 702 }).CreateEntity();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(702).Count != 1) {
            Console.WriteLine("FAIL: >64 索引 CreateEntityBatch 路径"); failures++;
        }

        // 8. >64 位索引：EntityBatch 添加、更新、删除路径
        var batchEntity = store.CreateEntity();
        batchEntity.Batch().Add(new IxHigh { Value = 703 }).Apply();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(703).Count != 1) {
            Console.WriteLine("FAIL: >64 索引 EntityBatch 添加路径"); failures++;
        }
        batchEntity.Batch().Add(new IxHigh { Value = 704 }).Apply();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(703).Count != 0 ||
            store.Query<IxHigh>().HasValue<IxHigh, int>(704).Count != 1) {
            Console.WriteLine("FAIL: >64 索引 EntityBatch 更新路径"); failures++;
        }
        batchEntity.Batch().Remove<IxHigh>().Apply();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(704).Count != 0) {
            Console.WriteLine("FAIL: >64 索引 EntityBatch 删除路径"); failures++;
        }

        // 9. >64 位索引：CommandBuffer 添加、更新、删除路径
        var commandEntity = store.CreateEntity();
        var addCommands = store.GetCommandBuffer();
        addCommands.AddComponent(commandEntity.Id, new IxHigh { Value = 705 });
        addCommands.Playback();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(705).Count != 1) {
            Console.WriteLine("FAIL: >64 索引 CommandBuffer 添加路径"); failures++;
        }
        var updateCommands = store.GetCommandBuffer();
        updateCommands.AddComponent(commandEntity.Id, new IxHigh { Value = 706 });
        updateCommands.Playback();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(705).Count != 0 ||
            store.Query<IxHigh>().HasValue<IxHigh, int>(706).Count != 1) {
            Console.WriteLine("FAIL: >64 索引 CommandBuffer 更新路径"); failures++;
        }
        var removeCommands = store.GetCommandBuffer();
        removeCommands.RemoveComponent<IxHigh>(commandEntity.Id);
        removeCommands.Playback();
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(706).Count != 0) {
            Console.WriteLine("FAIL: >64 索引 CommandBuffer 删除路径"); failures++;
        }

        // 10. >64 位索引：CopyEntity 更新与删除路径
        var copySource = store.CreateEntity(new IxHigh { Value = 707 });
        var copyTarget = store.CreateEntity(new IxHigh { Value = 708 });
        EntityStore.CopyEntity(copySource, copyTarget);
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(707).Count != 2 ||
            store.Query<IxHigh>().HasValue<IxHigh, int>(708).Count != 0) {
            Console.WriteLine("FAIL: >64 索引 CopyEntity 更新路径"); failures++;
        }
        var copyWithoutIndex = store.CreateEntity(new C70 { V = 70 });
        EntityStore.CopyEntity(copyWithoutIndex, copyTarget);
        if (store.Query<IxHigh>().HasValue<IxHigh, int>(707).Count != 1) {
            Console.WriteLine("FAIL: >64 索引 CopyEntity 删除路径"); failures++;
        }

        // 11. 生成器真实泛型参数与注册分派
        var stringEntity = store.CreateEntity(new IxString { Value = "generator-string" });
        if (store.Query<IxString>().HasValue<IxString, string>("generator-string").Count != 1) {
            Console.WriteLine("FAIL: IIndexedComponent<string> 注册"); failures++;
        }
        var linkTarget = store.CreateEntity();
        var linkEntity = store.CreateEntity(new LinkHigh { Target = linkTarget });
        if (store.Query<LinkHigh>().HasValue<LinkHigh, Entity>(linkTarget).Count != 1) {
            Console.WriteLine("FAIL: IIndexedComponent<Entity> 注册"); failures++;
        }
        var intRelationEntity = store.CreateEntity();
        intRelationEntity.AddRelation(new RInt { Key = 17 });
        bool foundIntRelation = false;
        foreach (var relation in intRelationEntity.GetRelations<RInt>()) {
            if (relation.Key == 17) foundIntRelation = true;
        }
        if (!foundIntRelation) { Console.WriteLine("FAIL: IRelation<int> 注册"); failures++; }

        // 12. record 与间接 Script 扫描
        var recordEntity = store.CreateEntity(new RecordC(9));
        if (recordEntity.GetComponent<RecordC>().Value != 9) {
            Console.WriteLine("FAIL: record struct 组件注册"); failures++;
        }
        var scriptEntity = store.CreateEntity();
        scriptEntity.AddScript(new ConcreteScript());

        Console.WriteLine($"friflo-mask: 测试完成, failures={failures}");
        if (failures == 0) { Console.WriteLine("friflo-mask: 验证成功——>64 位关系/索引掩码及生成器健壮性已修复"); }
        else { Environment.Exit(1); }  // FORK-CUSTOM：失败时非零退出（CI 门禁）
    }
}
