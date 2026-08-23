# CHANGELOG_BAIZE.md —— baize-godot fork 定制流水账

> 本文件是 baize-godot（Godot Fork）相对上游定制的**流水账记录**，按时间倒序追加。
> 起始日：**2026-08-21**（All-in C# 路线定案日，此前探索均已放弃且已删除，不记录）。
> 每笔定制：日期 + 改动 + 目的（一句话）。新增定制直接在顶部加一条，无需分类。
> 上游基线：Godot 4.8-dev（merge-base `7a3904e22b`，2026-07 上游 master）
> 决策唯一权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_All-in-CSharp_总方案.md`

---

## 2026-08-22
- **Object+Components 落地：O0 源码依赖地图**（`doc/plans/object-components/O0-源码依赖地图.md`）：实测量化 Node/NodePath/SceneTree/PackedScene 依赖面（Node 直接派生 28 类、Node3D 34 文件、CanvasItem/Node2D/Control 101 类、NodePath 1308 处、SceneTree 1057 处、PackedScene 344 处）；每个 Node 派生类给出迁移分类 + 后端 owner；目标分层 §14.5（GameWorld 纯运行时 / BaizeMainLoop / GameWorldNodeHost / EditorPreviewHost），分支 `feature/object-components-engine`。
- **Object+Components 落地：O1 语义契约**（`doc/plans/object-components/O1-GameObject语义契约.md`）：回答 §14.8 十条（组件重复/依赖/enabled 传播/生命周期顺序/tick 生效时机/销毁后句柄/父子职责/Prefab Override 原则/Relation 清理/确定性序列化白名单）+ Services 端口；先定契约再写代码。
- **Object+Components 落地：O1 纯 GameWorld 内核（modules/gameobject/，纯 .NET 零依赖）**：EntityId=Index+Generation 防复用、GameComponent 生命周期（OnCreate/OnEnable/OnStart/OnTick/OnFixedTick/OnDisable/OnDestroy）、ComponentSchema 注册表（[GameComponent] 单/多实例 + Requires 依赖）、ObjectHierarchy（环检测 + 销毁级联摘除）、RelationGraph（双向索引 + 类型工厂注册表）、GameWorld（对象 registry/快照 tick/同步销毁/Services/Undo-Redo 栈）、GameWorldSerializer（确定性 Capture/Restore/FNV-1a 64 hash）、EditTransaction（命令模式 Undo/Redo）。验收 `test-projects/gameobject-core-tests/`：**142 项断言全绿**（Debug + Release，reviewer 五轮闭环：16+6+3+1 项 P1/P2 findings 全修复）。不引用 Friflo/Baize.Ecs/Godot，不接触 Node（O1 硬门禁达成）。
- **Object+Components 落地：社区对象模型对照与借鉴**（`doc/plans/object-components/社区对象模型对照与借鉴.md`）：对照 Unity/DOTS、Flecs、Bevy、EnTT、Stride、WaveEngine、Defold 七个成熟模型——O1 内核（身份/生命周期/关系/预置体/场景格式）与社区高度同构；采纳决策 B1-B6（B1 Required Components O2 前强制、B2 DataContract 序列化对齐 O3、B3 prefab override 照 Unity O4、B5 Stride processor 模式 O6+、B6 Defold 消息队列按需）；明确不借鉴 DOTS Archetype/EnTT 池作默认存储（方案 §14.2 有意决策）。O1 契约 R20 + AGENTS 索引已登记。
- **Object+Components 落地：O2 GameObject-first Shooter**（`test-projects/shooter-object-components/` 共享库 + `shooter-object-components-poc/` 验收）：玩法全部落在 GameObject+Components——数据组件（Position/Velocity/Health/WeaponConfig/...）+ 标记组件（PlayerFaction/EnemyFaction/ProjectileTag 取代 ECS Tag）+ 行为组件（MoveObject/PlayerInput/FireWeapon/Projectile/EnemyAI/EnemySpawner 取代 ECS System，GameOver 冻结用 IsPlaying 门禁）+ 服务（MatchState/Input/SpawnConfig/SpawnState）。B1 落地：ShooterFactory 一行创建带全套组件（创建零样板）；Requires 依赖链生效。验收 8 场景全绿（移动/Fire 边沿/扫掠命中/同 Tick 去重/四面生成覆盖/寻敌/GameOver 冻结/旧句柄拒结算/重启），硬门禁达成：零 Friflo/Baize.Ecs/Godot/Node，Gameplay 纯 C# 直调。
- **Object+Components 落地：O2 干净 GameObject-first 重构**（`test-projects/shooter-object-components/`）：应评审要求移除 ECS 痕迹，重写为纯「对象+组件」直调——去掉 CommandBuffer/FlushFrame/逐帧 IsPlaying 门禁/全局命中计分仲裁器，改为一套 **MotionPlan**：`ShooterGame.Step` 于 `world.Tick` 前先跑「PlanMotion 阶段」（PlayerController→EnemyController→BulletBehavior 依次 `PlanMotion(delta, tickIndex=world.TickIndex+1)` 提交本 tick 不可变计划到组件），移动与命中共同消费该冻结计划。命中为顺序无关扫掠：`BulletBehavior.OnTick` 用自身+敌方 `MotionPlan` 做 `SegmentSegmentDistance`（相对扫掠 `r(t)=(A0-B0)+t((A1-A0)-(B1-B0))`，最短距离=原点到线段 `[(A0-B0),(A1-B1)]`），`enemyPlan.TickIndex==world.TickIndex` 门禁符合 O1「tick 内新建对象下一轮参与」快照语义。`ShooterWorld.CanTick` 等效 O1 `IsTickable`（对象/父链有效启用+组件 Enabled），三个 `PlanMotion` 对未启用组件提交静止计划，避免禁用玩家控制器误动、禁用敌人行为产生幽灵轨迹。命中直接调 `enemy.Health.ApplyDamage` → `MatchController.OnEnemyKilled`。验收 `test-projects/shooter-object-components-poc/` **15 项全绿**（移动/Fire 边沿/扫掠命中/顺序无关命中/玩家帧内移动规划=实际/禁用行为静止/禁用控制器不移动/小幅相对扫掠/四面生成覆盖/寻敌/GameOver 冻结/旧句柄拒结算/重启/确定性），硬门禁达成：零 Friflo/Baize.Ecs/Godot/Node，Gameplay 纯 C# 直调。
- **Object+Components 落地：O2 Human-first 说明文档**（`test-projects/shooter-object-components/HUMAN_FIRST_AUTHORING.md`）：以 Human-first 视角讲清 GameObject+Components 设计——核心概念（GameObject/GameComponent/Service/MotionPlan/工厂）、组件四分类、六步心智模型、与 ECS 对照、最终检查清单；便于新开发者「先读 `ShooterGame.Install`+`Step`，再按文件逐层深入」。AGENTS §14 索引已登记。

## 2026-08-21（起始日）
- 新增 `CHANGELOG_BAIZE.md`：fork 定制流水账，从今日起记录与上游的差异。
- 定案 **All-in C# 路线**（决策唯一权威：`Godot_Fork_All-in-CSharp_总方案.md` v3.6）——战略宪法/技术路线/架构模式/生态集成/实施路线。
- **P0 实施完成（net11 切换）**：global.json 锁 11.0.100-preview.7；12 个引擎程序集切 net11.0 + LangVersion latest（C# 15 预览期写法）；Source Generator 保持 netstandard2.0；4 个 scons-profile 内建 mono + 禁 GDScript；site_scons 全链路 UTF-8；C# 冒烟项目 `test-projects/csharp-check` 实测通过（打印 "All-in C# 验证成功 (net11)"）。
- **P1 实施完成（无 GDScript 引擎）**：无 GDScript + mono 引擎构建/启动验证通过（gen.h 仅 MODULE_MONO_ENABLED）；修复禁用后暴露问题——script_text_editor.cpp 的 gdscript 设置查询加 #ifdef 保护（消除 WARNING）、HotReloadAssemblyWatcher.cs 的 Timer.Start 加树内检查（FORK-CUSTOM，修复 headless 崩溃 0xC0000005）；test-projects/.gitignore 排除 .godot 缓存/.uid。
- **创建项目即 C#**：project_dialog.cpp 新建项目 features 自动带 "C#"（MODULE_MONO_ENABLED 时）；GodotSharpEditor _EnablePlugin 自动创建 csproj/sln（无需手动"项目→工具→C#"或等 .cs 文件）——空项目打开即生成 net11 C# 工程（验证通过）。
- **解决方案默认 .slnx**：DotNetSolution.Save() 默认生成 .slnx（XML，生态新标准；加载侧本就支持），保留 .sln 回退（GenerateSlnx=false）——验证：dotnet build .slnx 通过、Godot 识别 .slnx 不另建 .sln。
- **阶段重排（shifu 裁决，总方案 v3.8）**：P1.5 C# Platform Contract（构建/运行/诊断/tool 生命周期收口）；P2 起 ECS 为唯一 Gameplay 权威（EcsWorldHost 替代每实体一 Node 桥接）；P3 Schema/Baker 提前；P6 拆 P6A/P6B；P1-R 延后到 P3 后。
- **一步到位（宪法 7，总方案 v4.0）**：示例游戏反思定案——目标即 Unity 式 Object+Components（非 Godot node 树），不混合渐进；Node 是隐形后端（隔离契约）；五世界分域权威（W1 编辑期/W2 Gameplay/W3 物理求解/W4 只读投影/W5 服务）；Scene DB 提前到 P2.4（最小 W1 Core）。
- **心智模型定案（总方案 v4.1）**：Object+Components ≈ ECS（Object≈Entity、Component≈Component、行为≈System）——业界验证过的最佳人类编辑外壳；W1/W2 两层表达同一模型 + 修正 7 个已知缺陷（隐式依赖/通信/膨胀/树滥用/Prefab/行为分散/生命周期）。
- **命名定案（总方案 v4.2）**：框架层 `EcsWorld`（ECS Kernel 改名，贴近 Bevy/Unity World）+ 宿主 `EcsHost`（EcsWorldHost 改名，避免混淆）。
- **P2.1 EcsWorld 框架实施（modules/ecs/）**：固定 Tick（TickIndex/FixedDelta）+ Step(InputFrame) + AddSystem(phase) + Reset；InputFrame（不可变输入帧）；EntityHandle（Id+Revision 安全句柄）；WorldCommandBuffer（链式创建 + Playback 归还池）；WorldEvents（事件总线）；ecsworld-smoke 冒烟测试 12 项断言全绿。
- **借鉴 Bevy（P2.1）**：`EcsResource`（全局单例，Bevy Resource——GameState/Score 不再设计成组件挂实体）；`IEntityBundle`（组件组合，Bevy Bundle——支撑 W1 Object=组件组合心智模型）；`EventWriter/EventReader`（读写分离，Bevy Event）。
- **同事协同论证（总方案 v4.4）**：独立确认 All-in C# 不荒谬——真正必须 C++ 只有 5%~15% 代码但占 50%~90% CPU 预算；分界原则"人在编辑器操作的东西优先 C#，每帧对海量数据/GPU 底层处理保留 native"；Godot Native Core 准确定位（Renderer/平台/生态接口保留，非性能敏感 C++ 上层迁移 C#）；3D Viewport 是 World 的一个 View（编辑器工具逻辑 C#，Renderer 保留 native）；性能优化次序（默认 C#→profile→hotspot→数据结构/算法→SIMD→才下沉）。同步到 §1.2.1 + §4.2 + §3.1。
- **社区调研佐证（总方案 v4.5）**：各引擎/语言"易用 vs 性能平衡"调研——混合架构(ECS 大规模模拟+对象表现)是主流(Unity DOTS 三层 Stratkit/Unreal Mass/Bevy 宏)；Bridge 层隔离(对应 EcsHost)；Archetypal ECS 非万能；Sparse-set vs Archetype 权衡(CommandBuffer 延迟变更)；先简单按需加复杂度。§3.1.1。结论：与社区共识一致，唯一长期方向是源生成器系统。
- **slnx review 修复（PR #5）**：改用官方 SolutionPersistence（SolutionModel + SlnXml）生成 slnx（schema 合法 BuildType + XML 转义）；LegacySolutionPath 精确清理另一格式（自定义名/双向切换）；csproj BOM。
- **CI 平台矩阵裁剪（PR #6）**：runner.yml 只保留 Linux+Windows+静态检查，移除 Android/iOS/macOS/Web 构建（平台文件保留可手动触发，符合宪法 6 先禁用后裁剪）。
- **基底定案：4.8-dev**（不切 4.7.2——4.8-dev 是 4.7 直系后代含全量功能 + mono 更新 + 零迁移，见总方案 §2.2）。
- **产品聚焦：风格化 3D 光谱**（覆盖 Anime NPR 三渲二 → Stylized PBR 全段，不做 2D 游戏、不做高写实 3D——见总方案 §1.3）。
- **宪法 6：先禁用后裁剪**（不用的功能不构建/不启用、源码保留，保上游合并亲和，深入定制后才物理删除）+ **风格化渲染架构 §1.4**（统一核心 + 风格化能力层 + Profile，shifu 审查定案）。
- AGENTS.md 重写为 All-in C# 路线（架构总览 D1-D6：4.8-dev 基底 / .NET 11 / 仅 C# / 少自研多集成 / ECS-first + Scene DB / 三级 Reload）。
- Taskfile.yml 精简（移除 verify-provider/TEST_PROJECT，dev-run 简化为 `--editor`）。
- `core/string/ustring.cpp/.h`：FORK-CUSTOM UTF-8 智能解码在案（中文优先宪法根基，commit b175d92bd6 + 审查修复 e08c1ea0f8）。
- `editor/animation/animation_track_editor.cpp`：`imported_anim_warning->hide()` 修复在案。
- `misc/scripts/build.py` + scons-profiles（win/mac dev/pro）+ `doc/customization/` 在案（构建体系）。
