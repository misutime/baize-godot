<!--
sola3d-godot vendor 信息：
- Upstream: https://github.com/friflo/Friflo.Engine.ECS
- Version: main 分支（commit 8e75b6b，2026-08-21 clone）
- License: MIT（LICENSE 文件随附）
- Vendor 范围: 仅 src/ECS（核心库）；src/CodeGen 只参考不 vendor；Tests/Demos 不 vendor
- 修改记录: PATCHES.md（FORK-CUSTOM 硬伤修复：关系掩码/Fliox 剥离/AOT 注册）
-->

[![friflo ECS](https://raw.githubusercontent.com/friflo/Friflo.Engine.ECS/main/docs/images/friflo-ECS.svg)](https://github.com/friflo/Friflo.Engine.ECS)        ![splash](https://raw.githubusercontent.com/friflo/Friflo.Engine.ECS/main/docs/images/paint-splatter.svg)

[![Wiki](https://img.shields.io/badge/GitHub-grey?logo=github&logoColor=white)](https://github.com/friflo/Friflo.Engine.ECS)
[![nuget](https://img.shields.io/nuget/v/Friflo.Engine.ECS?logo=nuget&logoColor=white)](https://www.nuget.org/packages/Friflo.Engine.ECS)
[![codecov](https://img.shields.io/codecov/c/gh/friflo/Friflo.Engine.ECS?logo=codecov&logoColor=white&label=codecov)](https://app.codecov.io/gh/friflo/Friflo.Engine.ECS/tree/main/src/ECS)
[![C# API](https://img.shields.io/badge/C%23%20API-22aaaa?logo=github&logoColor=white)](https://github.com/friflo/Friflo.Engine-docs)
[![Discord](https://img.shields.io/badge/Discord-5865F2?logo=discord&logoColor=white)](https://discord.gg/nFfrhgQkb8)
[![Wiki](https://img.shields.io/badge/Wiki-A200FF?logo=gitbook&logoColor=white)](https://friflo.gitbook.io/friflo.engine.ecs)

# Friflo.Engine.ECS

High Performance C# ECS - Entity Component System.  
***The ECS for finishers 🏁***  


## Feature highlights
- [x] Simple API - no boilerplate, rock-solid 🗿 and bulletproof 🛡️
- [x] High-performance 🔥 compact ECS
- [x] Low memory footprint 👣. Create 100.000.000 entities in 1.5 sec
- [x] Zero ⦰ allocations after buffers are large enough. No struct boxing
- [x] High performant / type-safe queries ⊆
- [x] Efficient multithreaded queries ⇶
- [x] Entity component Search in O(1) ∈ 
- [x] Fast batch / bulk operations ⏩
- [x] Command buffers / deferred operations ⏭️
- [x] Entity relationships and relations ⌘
- [x] Entity hierarchy / tree ⪪
- [x] Fully reactive / entity events ⚡
- [x] Systems / System groups ⚙️
- [x] Watch entities, components, relations, tags, query results, systems, ... in debugger 🐞
- [x] SIMD Support 🧮
<!-- FORK-CUSTOM（P2-2）：JSON Serialization 已剥离（Fliox 删除，序列化职责归 MemoryPack/STJ） -->
- [x] Supports .NET 11（FORK-CUSTOM：All-in C# net11 独占）
- [x] **100% secure C#** 🔒. No *unsafe code*, *native dll bindings* and *access violations*. 
  See [Wiki ⋅ Library](https://friflo.gitbook.io/friflo.engine.ecs/package/library#assembly-dll).  


More at GitHub [README.md](https://github.com/friflo/Friflo.Engine.ECS)


## What is an ECS?

An entity-component-system (**ECS**) is a software architecture pattern. See [ECS ⋅ Wikipedia](https://en.wikipedia.org/wiki/Entity_component_system).  
It is often used in the Gaming industry - e.g. Minecraft - and used for high performant data processing.  
An ECS provide two strengths:

1. It enables writing *highly decoupled code*. Data is stored in **Components** which are assigned to objects - aka **Entities** - at runtime.  
   Code decoupling is accomplished by dividing implementation in pure data structures (**Component types**) - and code (**Systems**) to process them.  
  
2. It enables *high performant system execution* by storing components in continuous memory to leverage CPU caches L1, L2 & L3.  
   It improves CPU branch prediction by minimizing conditional branches when processing components in tight loops.


