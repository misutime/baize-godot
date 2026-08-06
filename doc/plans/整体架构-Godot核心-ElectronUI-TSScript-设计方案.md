# 整体架构设计方案：Godot 核心 + Electron UI + TS 脚本层

> 状态：设计落地版（v1，2026-08-06）。后续渐进讨论优化。
> 基线事实：本 fork = Godot 4.8 dev（version.py major=4 minor=8）。一切设计以本项目源码与
> 官方文档（D:/misutime/104_game/godot-docs）为真相。

---

## 1. 目标

1. **做减法**：移除 CEF 嵌入（att_webview）、Node sidecar（att_nodejs_sidecar），回到定制前；
2. **全新 UI**：Electron 应用承载完整编辑器 UI（React），Godot 只做"引擎核心 + 能力暴露"；
3. **TS 脚本层**：脚本完全使用 TypeScript（游戏逻辑 + 工具脚本），Godot 进程内以 QuickJS 执行；
4. **数据真相**：场景树/资源/undo/选择/项目状态一律以 Godot 核心为准，Electron 是纯投影与交互入口；
5. 三个层面（Electron UI / Godot 核心 / TS 脚本）作为**一个整体**设计，非割裂讨论。

### 双阶段目标（2026-08-06 确认）

- **并存期（短期，默认）**：Electron UI 与 Godot 原生 UI **双前端并存**——同一 Godot 二进制（原生编辑器 + headless 核心模式），共享磁盘真相；Electron 功能渐进式添加（能力面扩展顺序 = UI 接管顺序），未实现的功能用户回原生编辑器操作（保存后切换，会话不共享、磁盘共享）。
- **最终态（长期）**：**完全取代**——打包的 Godot 只有核心（编辑器逻辑 + 渲染服务 + 能力服务 + QuickJS），无原生 UI；原生 UI 代码编译期剔除。
- 推论：双阶段使架构**渐进、可回退**——Electron 路线受挫时原生编辑器随时可用。

## 2. 事实基础与关键决策

### 2.1 Electron 能否直接调用 Godot C++？（探索结论）

| 候选路径 | 结论 | 依据 |
|---|---|---|
| Node 加载 GDExtension | ❌ 方向反了 | GDExtension 是"Godot 进程加载扩展"（进程内 C ABI）；Electron 进程无法被 Godot 加载，也无法加载 Godot 的扩展（扩展依赖 Godot 提供的函数指针） |
| LibGodot（官方库形态） | ❌ 本 fork 无此资产 | 官方 FAQ 称 Godot 4.6 起有**实验性** LibGodot（Windows/macOS/Linux，面向 C++ 宿主）；本 fork（4.8 dev）**无 libgodot 代码**（`ls libgodot` 不存在、无 godot_initialize 符号） |
| 即便引入 LibGodot + N-API .node 绑定 | ❌ 不成立/不推荐 | ① 双主循环：Node libuv 与 Godot 主循环共存需逐帧 iterate 集成（Android library 形态先例是宿主每帧调 GodotLib.iterate，Node 侧需自研）；② 渲染上下文：Godot 需 GPU 窗口上下文，Electron 主进程无原生渲染窗口；③ 编辑器形态：LibGodot 面向游戏运行时嵌入，编辑器（EditorNode）嵌入无先例；④ 崩溃耦合：进程内 = Godot 崩溃即 Electron 全崩，官方 game embedding 明确以独立进程隔离崩溃 |
| **独立进程 + IPC（采用）** | ✅ | 官方既定模式：编辑器 ⇄ 游戏**始终独立进程 + 网络调试协议**（game_embedding.rst："The game always runs in a separate process"；EditorDebuggerServerTCP 监听 `network/debug/remote_port`）；Electron 自身即主/渲染进程 IPC 架构，增加 Godot 进程与之同构 |

**决策 1：Electron 的 Node 通过进程间通信（WS/JSON-RPC）向 Godot 进程发语义命令；能力实现仍住在 Godot 进程内。** 这是唯一现实路径，且与官方编辑器生态模式一致。

### 2.2 Godot 侧其余关键事实

- **headless 编辑器官方支持**：`EditorNode` 构造首行 `cmdline_mode = (DisplayServer::get_singleton()->get_name() == "headless")`（editor/editor_node.cpp:8399），headless 下跳过预览线程/进度框等 UI 专属操作，**逻辑状态机完整**（CI 导出即 headless 编辑器）；`EditorNode` 创建只依赖 `editor` 标志（main/main.cpp:4497-4507）。
- **headless 无渲染**：`--headless` = display headless + audio Dummy + dummy rasterizer（main/main.cpp:565,1432）——视口渲染必须有真实 GPU 上下文（见 §6）。
- **自定义脚本语言官方机制已铺好**：`ScriptLanguageExtension`（core/object/script_language_extension.h，1033 行接口）被 GDExtension 接口引用（gdextension_interface.cpp:40）；`ScriptServer::register_language` 是公开机制（gdscript/mono 均如此注册）。**本 fork 无任何 JS/TS 运行时模块**——QuickJS 集成需自建。
- **官方能力基线**：`EditorInterface`（edited_scene_root/selection/undo_redo/resource_filesystem/previewer/save/open/play/theme/scale…，editor/editor_interface.h:99-174）覆盖大半场景操作能力。
- **游戏运行官方机制**：Play = 独立游戏进程 + 调试协议（远程场景树/属性编辑已有官方实现）。
- **console exe（Windows）**：`scons p=windows target=editor` 自动产出两个 exe——GUI 版（windows subsystem）与 console 版（console subsystem，`platform/windows/detect.py:224` windows_subsystem、`SCsub:126`）。**console ≠ 无 UI**：引擎逻辑与 UI 完全一样，仅日志可见性不同（从终端运行 print_line/print_error 直出，`os_windows.cpp:183` RedirectStream CONOUT$）；无 UI 由 **headless 运行参数**决定（`--headless`），两者正交。

### 2.3 决策汇总

| # | 决策 | 理由 |
|---|---|---|
| D1 | Godot 独立进程 + IPC（WS/JSON-RPC），不做进程内直调 | §2.1 |
| D2 | Godot 默认 headless 编辑器模式运行（逻辑完整、零 UI 开销），视口渲染另配 GPU 上下文 | §2.2 |
| D3 | TS 脚本以 QuickJS 内嵌 Godot 进程执行（游戏逻辑）；工具/自动化脚本可走 Node 侧同一能力面。**语言实现 = fork 模块直接注册**（`ScriptServer::register_language`，同 gdscript/mono 机制；实现类直接继承 `ScriptLanguage`，不走 GDExtension/ScriptLanguageExtension 路径） | 性能（游戏逻辑不可走 IPC）+ fork 深定制已既定（String/字体/platform），GDExtension 无隔离收益；ScriptLanguageExtension 仅是 ScriptLanguage 的 GDExtension 适配子类（多一层 GDVIRTUAL 包装） |
| D4 | 单一协议 + 类型单源（@baize/godot-rpc），所有进程外消费方共用 | 消除双通道漂移（当前 sdk/sidecar 协议异构的收敛） |
| D5 | 数据真相一律在 Godot；Electron 只发语义命令、收事件投影 | 单一事实源，防状态分叉（历史教训：scene.create_node 默认名分叉） |
| D6 | 能力面以 Registry 为唯一事实源，通道只做协议适配 | 沿用并强化现有实施原则 |
| D7 | 双前端并存（原生 UI + Electron），共享磁盘真相、会话不共享；最终态完全取代原生 UI | 用户确认；渐进可回退；能力面成熟度 = Electron UI 覆盖度 |
| D8 | **传输定案：WS over TCP loopback（唯一通道）**，不预建 named pipe/UDS/共享内存/stdio 等替代传输；序列化维持 JSON，不预建二进制升级 | 用户决策（2026-08-06）：性能不是命令通道的量纲，工程成本与调试性优先；视口帧传输问题留到视口策略 B/C 决策时再议（M1 策略 A 无帧传输，不出现该问题） |

## 3. 总体架构

```
┌─ Electron 应用（全新 UI 层）────────────────────────────────────┐
│ 渲染进程（React）                                                │
│ ├─ 场景树面板 / Inspector / 资源库 / 工具栏 / 设置 / 对话框        │
│ └─ 视口面板（帧显示，见 §6 视口策略）                             │
│        ▲ Electron IPC（contextBridge）                           │
│ 主进程（Node.js）                                                │
│ ├─ GodotClient：spawn Godot、WS 连接、JSON-RPC、事件订阅、        │
│ │               崩溃检测与重启、会话恢复                          │
│ ├─ 窗口管理（主窗/多窗/布局）                                    │
│ ├─ 菜单/快捷键/系统集成（原生能力，如文件对话框、外部打开）        │
│ └─ 生命周期编排（启动/退出/恢复）                                 │
└──────────────┬───────────────────────────────────────────────────┘
               │ WS/JSON-RPC 2.0（唯一外部通道，127.0.0.1:0 + 双令牌）
               ▼
┌─ Godot Core（Godot 进程）─────────────────────────────────────┐
│ Godot Provider（对外服务出口）                                  │
│ ├─ Ops：操作层（官方 API 编排成语义用例，~80% 薄封装）           │
│ ├─ Registry：能力清单（project.*/scene.*/...，schema/错误码/事件） │
│ ├─ Transport：传输层（WS/JSON-RPC/认证/预算）                     │
│ └─ Events：事件层（diff 推送 selection/undo/scene/resource/run）  │
│ 编辑器核心：EditorNode 状态机（Provider 的底层依赖，headless）   │
│ 渲染服务：视口 GPU 上下文（策略 A/B/D，§6）                       │
│ TS 运行时：QuickJS（游戏逻辑/工具脚本，独立线）                   │
│ 游戏运行管理：spawn 独立游戏进程 + 调试协议接线（官方机制）       │
└──────────────┬───────────────────────────────────────────────────┘
               │ 官方调试协议（TCP）
               ▼
        游戏进程（Play 时；独立，官方机制；视口显示按 §6 策略）
```

**数据流原则**：UI 交互 → 语义命令（RPC）→ Godot 执行（undo 入栈）→ 事件 diff 回推 → UI 投影更新。Godot 是唯一状态权威，Electron/TS 工具侧**不缓存可变更状态**。

### 3.0 术语定义（2026-08-06 定案）

| 术语 | 定义 |
|---|---|
| **Godot Core** | Godot 进程整体：引擎 + 编辑器核心 + 渲染服务 + QuickJS 运行时 + Provider |
| **Godot Provider** | Core 内的**对外服务出口**：能力面（Ops + Registry）+ 传输（Transport）+ 事件（Events）。**不含**：QuickJS 脚本运行时、视口渲染服务、编辑器核心本身（EditorNode 是其底层依赖）。能力内容 ~80% 为官方 API 薄封装 |
| **GodotClient** | Electron/Node 消费方（@baize/godot-sdk / godot-process 内实现）：WS 连接/RPC/事件订阅/生命周期 |
| 能力面四层 | **Ops**（操作层：官方 API 编排成语义用例）/ **Registry**（能力清单：方法/参数/错误码/事件声明）/ **Transport**（传输层：WS/JSON-RPC/认证/预算）/ **Events**（事件层：diff 推送） |

命名对称：**Godot Core（进程）→ Godot Provider（服务出口）→ GodotClient（消费方）**。

### 3.1 数据真相模型

数据真相分三层，**互通性来自第一层（磁盘文件）与统一的保存语义，不是进程同步**：

| 层 | 载体 | 归属 | 说明 |
|---|---|---|---|
| 持久化真相 | 磁盘：`.tscn`/`.tres`/`project.godot`/`.uid`/`.import` | 共享（任何编辑器可读写） | 跨工具互通的最终保证——Electron UI 保存、原生编辑器打开看到的是同一份文件 |
| 会话状态 | 编辑器进程内存：打开的场景/选择/undo 栈/脏标记 | **每进程私有** | 与"两个原生编辑器实例开同一场景"行为一致：各改各的，无实时同步；保存即写盘 |
| 运行真相 | 游戏进程内场景树实例 | 游戏运行临时态 | 独立进程，官方机制 |

互通机制（关键约束）：
- **Electron UI 不直接读写 .tscn**——一切修改经能力面操作 Godot 进程内场景对象，保存走 Godot 保存管线（EditorNode 序列化，与原生同一套代码）→ 文件格式 100% 兼容，不自行实现 .tscn 解析；
- 并发语义与原生双实例一致：外部修改检测机制（原生已有）可复用，不做实时同步；
- **验收标准：Electron UI 保存的场景，用原生编辑器打开内容一致（等价于原生编辑器自己保存的结果）**。

## 4. 统一协议与能力面（Godot Provider）

### 4.1 协议（沿用 sidecar_server 线级合同，唯一通道）

- JSON-RPC 2.0 严格子集：一帧一 document、request id 一律 string、batch 显式拒绝（-32600）、server 拒 response；错误码 -32601/-32602/-32000 + 内部字符串码入 `error.data.code`。
- 传输：WS over 127.0.0.1:0（Godot listen，Electron 主进程连回；端口经 spawn env 下发）——现有 sidecar_server 机制直接平移。
- **传输定案（D8，2026-08-06）**：WS 为唯一传输，不预建替代方案；JSON-RPC + JSON 序列化维持现状；视口帧等高频数据问题留到视口策略 B/C 决策时再议（M1 策略 A 无帧传输）。
- 认证：spawn 时双令牌（随机 32B hex，仅内存 + env）；hello 首帧校验 + deadline。
- 事件下行：Godot→Electron 单向 notification（diff 推送，沿用现有机制；事件声明 schema 归 Registry）。

**完整调用链路示例（scene.create_node）**——一次能力调用从 SDK 到引擎的旅程：

```
Electron/Node（@baize/godot-sdk）
  scene.create_node({name:"Cube"})           ← TS 方法（网络 + 类型封装）
    │ WS 发送：{"jsonrpc":"2.0","id":"r1",
    │          "method":"scene.create_node","params":{"name":"Cube"}}
    ▼
Godot Transport（WS server 收帧，解析信封）
    │ Registry.find("scene.create_node")     ← 按名查注册表
    │ Registry.validate_args：校验 {"name":"Cube"} 满足 schema（必填/类型）
    │  （失败 → 回 -32602 Invalid params）
    ▼
handler（薄转发）
    │ return Ops::create_node(args["name"]);
    ▼
Ops（真实实现）
    │ EditorUndoRedoManager::create_action("AI Create Node")
    │ + add_do_method(root,"add_child",node,true) + add_undo_method(...)
    │ → 编辑器场景树变更（undo 入栈，与人工操作一致）
    ▼
应答：{"jsonrpc":"2.0","id":"r1",
       "result":{"instance_id":123,"path":"/root/Cube","name":"Cube"}}
    ▼
SDK 解析 → scene.create_node() 返回结果 → Electron UI 刷新（Events diff 推送）
```

错误路径（三类）：方法不存在 → Registry.find 返回 nullptr → -32601；参数不合法 → validate_args 失败 → -32602（内部字符串码入 data.code）；业务失败（如无场景）→ handler 返回 `{ok:false,error}` → -32000 + data.code。

**关键性质：Electron 不认识 Godot 内部任何类**——它只认识方法名 + schema + 信封；Godot 内部结构（EditorUndoRedoManager 等）被 Ops/Registry 完全隔离在 Provider 内。

### 4.2 能力面定义（Registry 唯一事实源）

| 命名空间 | 方法（首批，渐进扩充） | 说明 |
|---|---|---|
| ~~ui.*~~ | **退役** | Godot UI 不再对外（UI 在 Electron）；语义 UI 树不需要了 |
| `scene.*` | get_tree（场景树快照）/ get_node / set_prop / get_prop / select / create / remove / undo / redo / get_position / set_position | 保留扩展现有能力；快照 + 增量 |
| `editor.*` | get_state / get_theme / get_scale / get_project_info | 编辑器状态与项目信息 |
| `project.*` | open / save / save_as / get_settings / set_settings / get_recent / close | **新增**（项目会话管理） |
| `resource.*` | list（文件系统）/ scan_status / import / load / save / get_preview / get_uid | **新增**（资源与导入管线） |
| `run.*` | play / stop / pause / step / get_state / attach_debugger | **新增**（游戏运行，接官方调试协议） |
| `viewport.*` | get_frame / set_camera / set_viewport_size / inject_input / get_cursor | **新增**（视口控制，策略相关） |

- 事件：`scene_changed` / `selection_changed` / `undo_stack_changed` / `resource_changed` / `run_state_changed` / `project_changed`（均带 payload schema）。

### 4.3 能力层设计：可复用 vs 必须新建（详细分析，2026-08-06）

> 结论先行：**能力内容的 ~80% 是官方 API 的薄封装（复用）**；工作量集中在能力面"壳"（Registry/分派/事件/传输）、场景树序列化、少量 API 化缺口。无大块"重新实现 Godot 能力"工程。

**可复用面（Godot 官方现成，引擎模块直接调用，无需绑定层）**：

| 能力域 | 复用来源 | 证据 |
|---|---|---|
| 场景操作 | `EditorInterface`（edited_scene_root/save/open/undo/selection）+ `EditorSelection` + `EditorUndoRedoManager` | editor/editor_interface.h:99-174 |
| 资源加载/保存 | `ResourceLoader`（load/线程化/缓存/进度）、`ResourceSaver`、`ResourceUID`（uid↔path） | core/io/resource_loader.h:249-251、resource_saver.h:86、resource_uid.h:71-85 |
| 文件系统/导入/预览 | `EditorFileSystem`（扫描/导入状态）、`EditorPaths`、`EditorResourcePreview`（预览图） | editor/filesystem/editor_file_system.h |
| 运行编排 | `editor_run`（play/stop）、`embedded_process`（游戏进程管理）、`EditorExport`（导出/构建/CI） | editor/run/editor_run.cpp、embedded_process.cpp |
| 调试协议（游戏进程） | `SceneDebugger` 全套（远程场景树/对象检查/保存节点/暂停/步进/调速）+ `EditorDebuggerNode`（会话/断点）——官方已有"外部查看/修改运行中状态"完整协议 | scene/debugger/scene_debugger.h、editor/debugger/editor_debugger_node.h |
| 引擎服务 | `ProjectSettings`/`Engine`/`OS`/`Input::parse_input_event`/`RenderingServer`/`PhysicsServer`/`SceneTree`/`PackedScene` | 公开 API |
| 传输 | sidecar_server（WS/JSON-RPC/认证/预算）——本 fork 资产 | 现有代码 |

**必须新建（Godot 侧）**：

1. **能力面框架**：Registry（方法/描述/JSON Schema/错误码/事件声明）+ JSON-RPC 分派（Transport）+ 事件源 diff（Events）——"壳"，全新写；模式可借鉴旧 att_editor_ops（schema/错误码/单源教训保留），**旧模块整体放弃**（方法域全新）；
2. **场景树 JSON 快照/增量序列化**：官方无现成（调试协议是游戏进程 NodeDump 格式，非编辑场景树）；自研：Node 遍历 + ClassDB 属性枚举 + 信号/脚本；
3. **undo 栈动作列表**：`has_undo/has_redo` 官方有，动作名称列表需 API 化；
4. **会话/项目管理 API 化**：最近项目/打开/保存/未保存确认（EditorNode 内部方法模块可直接调，如 `open_scene`，封装为能力）；
5. **视口渲染服务**（策略 A/B/D）：独立窗口/离屏渲染 + GPU 共享——M1+；
6. **headless UI 树抑制**（M3）：EditorNode 构造/Notification 引擎改动；
7. **QuickJS 语言模块**：独立线，与能力层解耦；
8. Electron 侧（GodotClient/UI 全量）：整体工程。

**能力域 × 来源矩阵**：

| 域 | 复用 | 新建 |
|---|---|---|
| `project.*` | EditorPaths/EditorSettings（最近项目）、EditorNode 打开/保存 | 会话 API 封装 |
| `resource.*` | ResourceLoader/Saver/UID、EditorFileSystem、EditorResourcePreview | 导入触发 API 化（scan 状态轮询） |
| `scene.*` | EditorInterface（树根/undo/selection/save/open）、EditorUndoRedoManager | 场景树 JSON 快照/增量、属性列表导出（ClassDB） |
| `run.*` | editor_run（play/stop）、embedded_process、SceneDebugger 全套、EditorDebuggerNode | 调试数据转推 Electron（复用协议消息，查看器 UI 在 Electron） |
| `viewport.*` | Viewport/SubViewport/RenderingServer/Camera3D、Input::parse_input_event | 视口渲染服务（策略 A/B/D） |
| `editor.*` | EditorSettings/EditorInterface 状态查询 | headless 编辑器状态（会话恢复） |
| `engine.*`（运行时域） | ProjectSettings/Engine/OS/RenderingServer/PhysicsServer、EditorExport（构建/CI） | 运行时服务封装 |

## 5. 三层一体化设计

### 5.1 Godot 核心层（Godot Core；Provider 见 §3.0/§4）

- **形态**：`--editor --headless`（逻辑完整）；渲染服务独立于 headless（§6）。若视口策略需要窗口，则窗口模式 + UI 树抑制。
- **二进制**：Electron spawn 目标 = **console exe**（日志可管道捕获）；`CREATE_NO_WINDOW` 隐藏控制台窗口（ProcessSupervisor 已有）。手动调试：终端直接跑 console exe + `--headless`。
- **编辑器核心**：复用 EditorNode 状态机（信号链可用），UI 树渐进裁剪（M3，可选优化——不要一开始就做）。
- **游戏运行**：`run.play` → spawn 独立游戏进程（官方机制）→ 调试协议接线（远程场景树/属性编辑官方已有）→ 视口按 §6 显示。
- **TS 运行时**：fork 新增 QuickJS 语言模块（D3）。

### 5.2 Electron UI 层

- 渲染进程：React 面板全量（场景树/Inspector/资源库/工具栏/设置/对话框/多窗口）；通过 preload + contextBridge 暴露受控 API。
- 主进程：GodotClient（@baize/godot-process：spawn/WS/生命周期）+ 窗口/菜单/原生集成 + 生命周期。
- 渲染进程不直连 Godot——一律经主进程转发（或经主进程暴露的 WS 代理），保持 Electron 安全模型。
- 对话框：编辑器原生对话框（节点选择/资源选择/创建）全部 Electron 化或 API 化（能力面提供候选数据）。

### 5.3 TS 脚本层（QuickJS）

- **语言注册（2026-08-06 定案，D3）**：fork 模块直接 `ScriptServer::register_language`（gdscript/mono 同机制，modules/gdscript/register_types.cpp:144），实现类直接继承 `ScriptLanguage`（core/object/script_language.h）——不走 GDExtension/ScriptLanguageExtension 路径（其 1033 行接口仅作实现清单参照：parse/validate/instance_create/属性/信号/补全/调试）。
- **构建期**：TS → JS（esbuild）+ .d.ts + sourcemap；产物随项目分发（类似 .gd 资源）。
- **运行时**：QuickJS 内嵌 Godot 进程；绑定层：Node/对象模型/信号/属性/方法调用（参照 gdscript 绑定模式实现 ScriptInstance）。
- **调试**：ScriptLanguage 调试接口 + QuickJS debugger 集成（渐进，M2+）。
- **双宿主**：
  - 游戏逻辑脚本：进程内直调引擎（性能路径，不走 IPC）；
  - 工具/自动化脚本：可进程内，也可跑在 Node 侧走能力面（命令粒度）。

### 5.4 统一 TS SDK（三包：@baize/godot-rpc / godot-sdk / godot-process，2026-08-06 定案）

**包结构与依赖**（均为纯 TS workspace 包，目录 = 包名）：

```
web/packages/
├── godot-rpc     @baize/godot-rpc    契约类型 + Transport 接口 + 配对/超时 + ws/ipc/inproc 实现（零依赖）
├── godot-sdk     @baize/godot-sdk    方法绑定（scene.create_node）+ 事件订阅 + react hooks   → 依赖 rpc
└── godot-process @baize/godot-process spawn Godot/生命周期/日志管道/转发（Electron 主进程用）   → 依赖 rpc
```

- **godot-rpc**：JSON-RPC 契约与传输核心（原 @baize/rpc 扩展为"类型 + 运行时"——双通道收敛方案 A 预告的演进）；传输实现按子路径导出（ws / ipc / inproc）；
- **godot-sdk**：能力面客户端（原 web/packages/sdk 改名重组）——同一套 `scene.create_node` 签名，任何环境可用：

```ts
import { scene, editor } from "@baize/godot-sdk";
// 渲染进程（transport=ipc）/ Node CLI·AI（transport=ws）/ 未来 QuickJS（transport=inproc）
```

- **godot-process**：Electron 主进程专属（原 web/runtime @baize/sidecar 改名迁移）——spawn Godot、生命周期、日志管道、渲染进程 IPC 转发；复用 godot-rpc 的 ws 传输（host 是转发者，不依赖 sdk 的方法绑定）；
- **transport 可插拔**：`inproc`（进程内直调）/ `ws`（Node 直连）/ `ipc`（渲染进程经主进程转发）——同一签名、同一类型、同一事件模型；
- 渲染进程安全模型：不直连 Godot WS（token 不出主进程），经 ipc transport → 主进程 godot-process 转发；
- 现有 `web/packages/sdk` 的 registry/bridge/类型化声明直接复用；transport 从 CefViewClient 换成 ws/ipc/inproc——**双通道收敛方案 A 的落地**。

## 6. 视口渲染策略（核心分叉）

事实约束：headless 无渲染（dummy rasterizer）→ 视口必须真实 GPU 上下文；编辑器视口 overlay（gizmo/网格/选择框）渲染管线在 Godot 侧；编辑器 3D 视口本质是 SubViewport 渲染到纹理（node_3d_editor_viewport.h:51-52）——改道输出改动集中；Godot 已有官方离屏渲染先例（--write-movie/MovieWriter）。

| 策略 | 机制 | 事实/历史 | 状态 |
|---|---|---|---|
| A. 独立窗口 | Godot 视口窗口（主窗口隐藏或专用窗口），Electron 面板并列 | 输入直达、零帧传输、最快闭环；"全新 UI"打折（视口非 Electron 内） | **M1 起步**（全平台） |
| B. 离屏帧传输 | SubViewport→texture→（CPU 读回）→Electron 显示 | CPU 读回 1080p 勉强、4K 吃力；输入需全转发；fork 已踩透 OSR 坑（resize/焦点/性能） | **过渡**（D 未完成前的无窗口形态） |
| C. 跨进程窗口嵌入 | Godot 视口子窗口嵌入 Electron 窗口（Win `SetParent`） | macOS NSView 跨进程不可行 | ❌ **否决**（必须同时支持 mac+win，跨平台不一致） |
| D. 无窗口离屏（GPU 零拷贝共享） | Godot 离屏渲染 → 跨进程共享纹理（Vulkan external memory / DXGI shared handle / IOSurface / dma-buf）→ Electron GPU 导入显示 | 零拷贝、全分辨率、60fps+；RenderingDevice 无现成跨进程导出 API，需 fork 定制（单次基础设施）；输入仍全转发 | **最终目标**（全平台像素级融合） |

**定案路线（2026-08-06）**：**A 起步（M1）→ B 过渡（无窗口）→ D 最终（无窗口 GPU 共享）**；C 否决。

- D = 最终形态：Godot 连视口窗口都没有，纯核心 + 离屏渲染，Electron 全包（UI + 视口显示）——与"最终打包 Godot 只有核心、没有 UI"契合；
- 无窗口方案的固有成本：**输入全转发**（Electron 捕获 → 坐标换算 → `Input::parse_input_event` 注入），鼠标/键盘/滚轮可工程化；
- B 与 D 共享无窗口/离屏/输入转发基础，B 先行可摊薄 D 的工程量（传输层从 CPU 读回升级为 GPU 共享）；
- 游戏进程视口同样适用本策略（Play 时独立窗口/离屏）。

### 6.1 交互能力确认（D 方案，2026-08-06）

**D 方案下编辑交互完整保留**（选择/拖动/gizmo/导航），机制为输入转发闭环：

```
Electron canvas 画面中拖动节点
  → Electron 捕获鼠标 → 坐标换算 → 输入注入（专用通道/IPC）
  → Godot Input::parse_input_event（公开 API）
  → 视口交互逻辑照常执行（Node3DEditorViewport 的拖动/gizmo/导航）
  → 帧更新 → GPU 共享纹理 → canvas 更新
```

- 依据：Godot 视口交互本质是"输入事件 → 编辑器逻辑"，与输入来源无关；官方已有输入注入先例（SceneDebugger::_handle_input/_handle_embed_input）与进程间控制先例（game embedding "Manipulate From Editors"）；
- 体验：1-2 帧转发延迟（16-33ms），编辑器交互可接受；优化方向 = 专用输入通道（不经能力面 RPC）+ 事件优先级；
- 画面为 60fps 连续帧流（非静态图片），交互闭环实时可见。

### 6.2 2026 社区调研补充（2026-08-06）

- **mac 无任何跨进程视图嵌入公开机制**：NSRemoteView 是私有 API（ViewBridge，仅苹果系统服务），第三方使用 = App Store 拒绝 + 随版本失效——C/伪嵌入在 mac 被系统层面封死；
- **macOS Sequoia 透明窗口系统级回归**：透明 overlay 致下层窗口被标记 hidden / 后台节流——伪嵌入（透明挖孔）在最新 macOS 有系统级性能惩罚，社区明确不建议（"Do not punch holes in the Chromium surface"、"Avoid Native Embedding — break with OS updates"）；
- **Godot 跨进程 GPU 共享仍非原生（2026）**：RenderingDevice 默认不启用 `VK_KHR_external_memory_*` 扩展——需**自定义编译 Godot 启用**（fork 定制可行且为唯一路径）；社区先例 `gd_module_texture_share_vk`；Windows 可参考 Spout 协议；**RD 导出需封装为薄隔离层**（RenderingDevice 低层互操作 API 不稳定，便于随引擎升级）；
- **WebGL canvas 渲染方案否决**：用 Three.js/BabylonJS 重画场景 = 另一套渲染器，材质/shader/特效不可能 1:1 复现 Godot → 破坏所见即所得（WYSIWYG）；WebGL 仅可作 B/D 的**帧显示**载体（显示 ≠ 渲染，渲染永远是 Godot）。

## 7. 生命周期与状态管理

- **启动**：Electron 主进程 → spawn **console exe**（`--editor --headless`，`CREATE_NO_WINDOW`，stdio 管道）→ WS 握手（token）→ `project.open`（最近项目/会话恢复）→ 资源索引事件 → UI 渲染。
- **日志管线**：Godot 的 print_line/print_error 经 stdio 管道被 Electron 主进程捕获 → 转发到 Electron 日志系统与渲染进程"输出面板"（替代原生 GUI 输出面板）；崩溃时 stderr 与退出码用于诊断。手动调试：终端跑 console exe + `--headless` 对照。
- **编辑**：UI 发语义命令 → Godot 执行（undo）→ 事件 diff 回推 → UI 投影更新。每帧高频路径（视口/相机）走 `viewport.*` 专用通道。
- **Play**：`run.play` → Godot spawn 游戏进程 → 视口显示 → 调试协议接线（官方）→ `run.stop`（退出编排：shutdown 通知 + 等 2s + kill 进程树，现有 ProcessSupervisor 平移）。
- **退出**：Electron → shutdown → Godot 保存确认（能力面提供未保存状态查询，保存对话框 Electron 化）→ 编排退出。
- **崩溃恢复**：Electron 检测 Godot 退出码 → 重启 + 会话恢复（能力面提供项目/场景/选择状态恢复查询）；游戏崩溃不波及编辑器与 Electron（进程隔离，官方模式）。

## 8. 与现有资产的映射

| 资产 | 处置 |
|---|---|
| `sidecar_server.{h,cpp}`（WS/JSON-RPC/认证/预算） | **复用**：平移为能力服务传输层（角色不变） |
| Registry（方法/描述/schema/错误码/事件声明） | **复用**：能力面骨架，方法重定义（§4.2） |
| 事件 diff 推送（selection/undo/scene） | **复用**：事件源机制 |
| `ProcessSupervisor` | **复用**：Godot spawn 游戏进程（Godot 侧）；Electron 侧由 child_process + CREATE_NO_WINDOW 承担（spawn console exe 防黑窗） |
| console exe（构建自动产出） | **复用**：Electron 被驱动核心（日志捕获）+ 手动调试 + 最终态发布形态（console subsystem 无窗口服务进程） |
| `web/runtime`（@baize/sidecar） | **改造迁移**：→ `packages/godot-process`（spawn/生命周期/日志/转发，Electron 主进程用） |
| `web/packages/sdk`（registry/bridge） | **改造改名**：→ `packages/godot-sdk`（transport 换 ws/ipc/inproc，方法绑定 + hooks） |
| `web/packages/rpc` | **改造改名**：→ `packages/godot-rpc`（类型 + 运行时：编解码/配对/传输实现） |
| `web/ui`（现 React 壳，已删） | **重建为 `web/app`**：Electron 应用（主进程 + 渲染进程 + preload） |
| `att_webview`（CEF 全链） | **删除** |
| `att_nodejs_sidecar` | **删除**（Godot 侧监督职责并入能力服务/移至 Electron） |
| `att_editor_ops` 的 ui.* / ui_tree | **退役**（语义 UI 树不再需要） |
| `att_editor_ops` 的 scene.*/editor.* | **并入**新能力面 |
| String UTF-8 智能解码 | **保留**（中文地基，与 UI 无关） |
| 字体体系（editor_fonts） | **视需要**：Electron UI 用系统/内置字体则退役 Godot 侧；视口 HUD 保留 |

## 9. 里程碑（渐进路线，2026-08-06 重组）

### 第 0 阶段：地基清理（2026-08-06 定案：新建 feature/electron-core 分支，立即执行）

**原则：不回退、不全 0 重写——保留 git 历史 + 清理提交**。理由：复用资产（String 智能解码/@baize/godot-rpc/godot-sdk/sidecar_server 传输经验）都在 WebDock 开发起点之后，回退会丢掉；git 历史本就为可追溯，代码现状干净即可。

| 项 | 处置 |
|---|---|
| 代码删除 | `modules/att_webview`（CEF 全链）、`modules/att_editor_ops`、`modules/att_nodejs_sidecar`（传输代码进 git 历史，第一阶段 Provider Transport 实现时参考移植）、`thirdparty/cefviewcore`、`web/ui` 旧壳、`misc/scripts/{cef_dist.py,stage_webview.py,stage_ui.py}` |
| 保留改造 | `core/string` 智能解码（中文地基，不动）、`web/packages/{rpc,sdk}`、`web/runtime`（→ Electron GodotClient） |
| 依赖处理 | `editor/themes/editor_fonts` 撤销 fork 改动（其依赖 att_ 模块 + 思源分发与旧架构绑定，恢复上游内置字体；中文回退 DroidSansFallback 仍在）；`misc/scripts/build.py` 恢复（去掉 CEF 预构建/暂存） |
| 文档归档 | `CEF集成选型/`、`页面渲染选型-OSR与非OSR/`、旧实施记录（AI-FIRST/NodeSidecar S1/WebUI/双向桥/实施原则）→ `已完成-历史文档/`；AGENTS.md fork 立场更新为新架构 |
| 仓库卫生 | SCsub 模块注册自然失效（目录删除）、`.gitignore`（bin/cef-dist 等）、Taskfile/justfile 更新 |
| 验证 | 原生编辑器构建通过（恢复"原版编辑器 + String 定制"；WebDock/sidecar 功能消失属预期） |

### 第一阶段：Electron UI ⇄ Godot 核心打通（优先）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M0 可行性 | headless 编辑器 + Godot Provider（复用 sidecar_server 传输 + 最小能力面）+ Electron 骨架（主进程连接 + 只读场景树/Inspector） | Electron 显示 Godot 场景树/属性，`--editor --headless` 稳定运行 |
| M1 闭环 | 视口策略 A + scene.*/editor.* 完整（选择/属性编辑/undo/保存） | 编辑-保存最小回路；Electron 完成场景编辑基本操作 |
| M2 完整 | project.*/resource.*/run.* + Play/调试（**GDScript 场景即可，官方机制，不依赖 QuickJS**）+ 对话框 Electron 化 + 事件完善 | 全编辑器主流程可在 Electron 完成 |

### 第二阶段：TS 脚本层（QuickJS）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| T1 语言管线 | fork 模块注册 TS 语言（ScriptLanguage 继承，D3 定案）+ QuickJS 内嵌 + 构建工具链（esbuild→JS）+ 绑定层（对象模型/信号/属性） | TS 脚本挂节点、进程内执行（hello 级） |
| T2 完整 | @baize/godot 的 inproc transport（进程内直调，与 ws transport 同签名）+ 调试器集成 + 游戏逻辑 TS 化迁移 | Play 场景脚本从 GDScript 迁移到 TS；TS 调试器可用 |

### 第三阶段：优化与最终态

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M3 优化 | **B 无窗口离屏过渡**（CPU 读回，D 前置基础设施：离屏渲染/输入转发）、**headless 跳过 UI 树构建**（运行时抑制，并存期即生效：省启动/内存）、协议收敛（删双通道）、删除 CEF/sidecar 旧代码、SEA 打包 | 干净架构，无遗留通道 |
| M4 最终态 | 原生 UI 编译期剔除（editor/ 目录 UI 与核心逻辑分离，大重构）；发布二进制 = 编辑器核心 + 渲染服务 + Godot Provider + QuickJS；视口 D（GPU 共享）按 §6.2 启动 | 发布版 Godot 无原生 UI，Electron 为唯一前端 |

> 阶段划分依据：QuickJS 是独立线（与能力层解耦，§4.3）；第一阶段 Play 用 GDScript 验证官方机制，不阻塞核心打通；inproc transport 设计可复用 ws transport 经验（第二阶段再做）。

## 10. 待决策点（渐进讨论议题）

**第一阶段（Electron UI ⇄ Godot 核心）**：

1. 能力面首批方法精确清单与 schema（§4.2 表逐步细化，project/resource 的边界）；
2. 事件推送粒度（diff 频率、快照 vs 增量、资源变更事件）；
3. 会话恢复细节（崩溃重启后的状态恢复范围）；
4. 认证/安全模型是否沿用（本机回环 + 双令牌）；
5. D 的 RD 导出启动时机（第三阶段，§6.2）。

**第二阶段（TS 脚本层）**：

6. QuickJS 绑定范围（首批暴露的引擎 API 面）与调试器集成深度（T1/T2 时再议）；
7. @baize/godot inproc transport 的设计（复用 ws transport 经验）。

---

## 附：证据索引

- 版本：`version.py:3-4`（4.8 dev）；LibGodot 缺席：无 `libgodot/` 目录、无 godot_initialize 符号。
- 官方库形态：godot-docs `about/faq.rst`（"Since Godot 4.6, there is experimental support…LibGodot"）。
- 官方嵌入/进程模式：godot-docs `tutorials/editor/game_embedding.rst`（"The game always runs in a separate process"）；`editor/debugger/editor_debugger_server.cpp:59-97`（TCP 调试协议）。
- headless：`main/main.cpp:565,1432,4497-4507`；`editor/editor_node.cpp:8399`（cmdline_mode）。
- 自定义语言：`core/object/script_language_extension.h`（1033 行）；`core/extension/gdextension_interface.cpp:40`；`modules/gdscript/register_types.cpp:144`。
- 官方能力基线：`editor/editor_interface.h:99-174`。
- 复用资产：`modules/att_nodejs_sidecar/sidecar_server.{h,cpp}`、`modules/att_editor_ops/registry.{h,cpp}`、`web/packages/{rpc,sdk}/`。
