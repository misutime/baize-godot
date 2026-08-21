# Friflo.Engine.ECS —— FORK-CUSTOM 修改记录

> 记录对 vendor 源码（thirdparty/Friflo.Engine.ECS）的所有修改，方便上游更新时重新应用。
> 格式：日期 + 改动 + 目的 + 涉及文件。

## 2026-08-21（vendor 初始）

| # | 改动 | 目的 | 文件 |
|---|---|---|---|
| V-1 | csproj 只目标 net11.0（上游多目标 net6-10 + ns2.1） | All-in C#：net11 独占（宪法 §1.2 第 3 条） | `Friflo.Engine.ECS.csproj` |
| V-2 | LangVersion latest（上游 14.0） | 跟随项目 C# 15 预览期写法 | `Friflo.Engine.ECS.csproj` |
| V-3 | 去 NuGet 打包配置 | vendor 本地库，不发布 | `Friflo.Engine.ECS.csproj` |
| V-4 | Fliox 依赖**暂保留** | P2-2 剥离为独立任务（届时删除 + 换 MemoryPack） | `Friflo.Engine.ECS.csproj` |

## 待办（P2 硬伤修复，进行中）

- [x] **P2-1 关系掩码 int → BitSet（256 位）**：isOwner/isLinked（EntityNode）+ indexBit（AbstractComponentIndex）+ relationBit（AbstractEntityRelations）全面 BitSet 化——40+ 类型关系/索引验证通过
- [x] **P2-2 Fliox 剥离**：删除 Friflo JSON 序列化层（Serialize/ 9 文件 + DataEntities/Unresolved/JsonUtils + StructHeap/ComponentType/ScriptType 的 Fliox 接口）——净删 2305 行；核心 ECS（存储/查询/关系/索引）不变；存档序列化由我们自己的 MemoryPack 层实现（§5.6）
- [ ] **P2-3 AOT 自动注册源生成器**：`Base/NativeAOT`（Roslyn 生成器自动收集组件注册）


