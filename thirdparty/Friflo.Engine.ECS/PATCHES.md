# Friflo.Engine.ECS —— FORK-CUSTOM 修改记录

> 记录对 vendor 源码（thirdparty/Friflo.Engine.ECS）的所有修改，方便上游更新时重新应用。
> 格式：日期 + 改动 + 目的 + 涉及文件。

## 2026-08-21（vendor 初始）

| # | 改动 | 目的 | 文件 |
|---|---|---|---|
| V-1 | csproj 只目标 net11.0（上游多目标 net6-10 + ns2.1） | All-in C#：net11 独占（宪法 §1.2 第 3 条） | `Friflo.Engine.ECS.csproj` |
| V-2 | LangVersion latest（上游 14.0） | 跟随项目 C# 15 预览期写法 | `Friflo.Engine.ECS.csproj` |
| V-3 | 去 NuGet 打包配置 | vendor 本地库，不发布 | `Friflo.Engine.ECS.csproj` |
| V-4 | Fliox 依赖**已剥离**（P2-2） | 删除 Friflo JSON 序列化层（Serialize/ 9 文件 + DataEntities/Unresolved/JsonUtils + StructHeap/ComponentType/ScriptType 的 Fliox 接口），csproj 去包；净删 2305 行 | `Friflo.Engine.ECS.csproj` + 41 文件 |
## 待办（P2 硬伤修复，进行中）

- [x] **P2-1 关系掩码 int → BitSet（256 位）**：isOwner/isLinked（EntityNode）+ indexBit（AbstractComponentIndex）+ relationBit（AbstractEntityRelations）全面 BitSet 化——40+ 类型关系/索引验证通过
- [x] **P2-1 补充：索引批量/复制路径完整 BitSet 化**：移除 EntityExtensions、EntityBatch/CreateEntityBatch、CreateEntity 泛型重载、CopyEntity、StructHeap 复制及 CommandBuffer 中残留的 `bitSet.l0` / `long` / `1 << Index`；改用完整 BitSet 的 Add/Remove/Intersect/HasAny/Has/IsDefault，覆盖 >64 位索引。涉及 `Entity/Extensions/EntityExtensions.cs`、`Batch/EntityStore.cs`、`Batch/CreateEntityBatch.cs`、`Entity/Store/Entities.cs`、`Entity/Store/Extensions/CreateEntity.cs`、`Archetype/Archetype.cs`、`Archetype/StructHeap.cs`、`Archetype/StructHeap.generic.cs`、`CommandBuffer/Commands/ComponentCommands.cs`
- [x] **P2-3 AOT 自动注册源生成器**：新建 thirdparty/Friflo.Engine.ECS.Generator（Roslyn IIncrementalGenerator，netstandard2.0）——自动收集 IComponent/ITag/IRelation/ILinkRelation/IIndexedComponent/Script；按真实泛型键类型分派 class/struct/Entity 注册，支持 record 与间接 Script，并对开放泛型、不可访问类型、Script 缺 public 无参构造发出 FECSGEN001-003；生成 EcsAotRegistration.RegisterAll(NativeAOT)，消除 AOT 手动注册清单遗漏



