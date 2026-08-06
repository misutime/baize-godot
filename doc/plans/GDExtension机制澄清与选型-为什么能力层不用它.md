# GDExtension 机制澄清与选型——为什么 Godot Provider 不用它作为能力层

> 面向团队：澄清 GDExtension 的准确定位，消除"GDExtension = Godot 暴露的能力接口、可直接使用、可提供所有功能、不需要 Ops 层"的误解。
> 事实来源：本项目 Godot 4.8 dev 源码 + 官方 Godot 文档（godot-docs）。
> 状态：2026-08-06。

---

## 0. 结论先行

1. **GDExtension 不是"Godot 暴露给外部的能力接口"，而是"外部 C++ 代码进入 Godot 进程的加载机制"**（进程内、Godot 主动加载它，方向是反的）。
2. **它暴露的是 ClassDB 白名单**（引擎类 + 部分编辑器类），**不是语义能力面**——没有 schema、错误码、undo 语义这些契约。
3. **进程外消费方（Electron/Node）根本无法使用 GDExtension**——它只在 Godot 进程内存在。
4. 本项目能力层（Ops）选择**引擎模块**而非 GDExtension，核心原因是**访问权**：能力实现需要 EditorNode 等引擎内部类与引擎改动，GDExtension 拿不到。
5. GDExtension 有明确适用场景（第三方插件分发、社区生态），但不是本项目核心能力层的载体。

---

## 1. GDExtension 是什么（官方机制）

- **历史**：Godot 3.x 的 GDNative → Godot 4.0 起更名为 GDExtension，是官方扩展机制（godot-docs `engine_details/engine_api/gdextension/`）。
- **形态**：扩展编译为共享库（`.dll` / `.so` / `.dylib`），由一个 `.gdextension` 配置文件描述；**Godot 进程在运行时加载它**（dlopen/加载库），通过 C ABI（`gdextension_interface.h` 的函数指针）与引擎交互。
- **绑定库**：`godot-cpp` 是官方 C++ 绑定——自动生成 ClassDB 暴露类的 C++ 包装（类名与引擎类同名，如 `EditorUndoRedoManager`）。
- **ABI 约束**：扩展针对特定 Godot 版本编译，与引擎版本强绑定（compat 机制支持有限跨版本）。

**关键点：加载方向**——是 **Godot 加载 GDExtension**（扩展住在 Godot 进程内），不是"外部程序加载 GDExtension 来调 Godot"。

## 2. GDExtension 能做什么 / 不能做什么

### 能（ClassDB 白名单内）

| 能力 | 说明 |
|---|---|
| 访问 ClassDB 暴露的类 | 引擎类（Node/Resource/SceneTree…）+ 编辑器类中暴露的（`EditorInterface`/`EditorSelection`/`EditorUndoRedoManager`/`EditorFileSystem`/`EditorSettings`…） |
| 注册新类进 ClassDB | 自定义 Node/Resource 等，可在场景中使用、被 GDScript 实例化 |
| 实现编辑器插件 | `EditorPlugin`：dock/工具栏/菜单/设置页/导入插件（社区插件生态的主流方式） |
| 注册自定义脚本语言（4.4+） | `ScriptLanguageExtension`（但它是 `ScriptLanguage` 的 GDExtension 适配子类，见 §4） |

### 不能（白名单之外）

| 限制 | 证据/说明 |
|---|---|
| 访问未暴露的编辑器内部类 | `EditorNode` **无 ClassDB 注册、无官方文档页**（godot-docs/classes/ 54 个 editor 类中无 class_editor_node；源码无 `register_class<EditorNode>`）；官方编辑器门面只有 `EditorInterface` |
| 修改引擎行为/内部实现 | 如 headless 跳过 UI 树构建需要改 `EditorNode` 构造/Notification——GDExtension 改不了引擎源码 |
| 接入模块级生命周期 | `register_types` 的 `MODULE_INITIALIZATION_LEVEL_*`（EDITOR 级首帧启动/退出编排）是模块特权 |
| 被进程外代码使用 | Electron/Node 无法加载 GDExtension（它依赖 Godot 提供的函数指针表） |
| 访问引擎私有单例/内部状态 | 未暴露的编辑器内部状态（会话/脏标记等）拿不到 |

## 3. 常见误解澄清（重点）

| # | 误解 | 事实 |
|---|---|---|
| 1 | GDExtension 是 Godot 暴露给外部的"能力接口"，直接调用即可 | 它是**外部 C++ 代码进 Godot 进程**的加载机制；暴露的是 ClassDB 类，不是语义能力面；**进程外（Node/Electron）根本用不了** |
| 2 | GDExtension 能提供所有功能 | 只有 ClassDB 白名单；`EditorNode` 等内部类、引擎改动拿不到（§2） |
| 3 | 有 GDExtension 就不需要 Ops 层 | **分层错位**：Ops 是语义能力面（契约/schema/undo 语义/守卫），GDExtension 是进程内访问机制；能力面需要的"契约 + 特权访问"GDExtension 都不提供 |
| 4 | GDExtension 是 Node.js/外部服务调用 Godot 的方式 | **方向反了**：Godot 加载 GDExtension（进程内）；外部服务只能走 IPC（本项目 = WS/JSON-RPC，即 Transport 层） |
| 5 | 用 GDExtension = 少改 Godot 源码 | 部分对（二进制隔离），但本项目已深定制（String 智能解码/字体/平台/headless），隔离收益不存在 |
| 6 | GDExtension 是唯一的官方扩展方式 | 还有：**引擎模块**（编译进引擎，gdscript/mono 同层）、编辑器脚本插件（EditorPlugin）、.NET 模块 |

## 4. 为什么本项目核心不选 GDExtension（六个原因）

| # | 原因 | 事实依据 |
|---|---|---|
| 1 | **能力实现需要 EditorNode 内部访问** | Ops 直取 `EditorNode::get_singleton()`（场景选择/状态）；语义 UI 树遍历整个编辑器树——EditorNode 不对 GDExtension 暴露 |
| 2 | **headless 定制必须在引擎内** | M3/M4 的"headless 跳过 UI 树构建"需改 EditorNode 构造/Notification |
| 3 | **生命周期时序是模块特权** | 现有 sidecar 即用 `MODULE_INITIALIZATION_LEVEL_EDITOR` 首帧启动（register_types.cpp） |
| 4 | **消费方是 Electron（进程外）** | 无论如何都需要 Transport（WS/JSON-RPC）；"有 GDExtension 就不需要 Ops/Transport"是对架构的错位理解（§3 误解 3/4） |
| 5 | **fork 深定制已既定** | String/字体/platform 都在改源码，GDExtension 的"少上游冲突"收益不存在；也不需要给第三方分发能力 |
| 6 | **TS 语言也不用 GDExtension** | 语言实现 = fork 模块直接 `ScriptServer::register_language`（gdscript/mono 同机制，modules/gdscript/register_types.cpp:144）；`ScriptLanguageExtension` 只是 `ScriptLanguage` 的 GDExtension 适配子类（script_language_extension.h:237），多一层 GDVIRTUAL 包装，fork 直接 override 基类更薄 |

## 5. 能力层载体对比：GDExtension vs 引擎模块

| 维度 | GDExtension | 引擎模块（本项目选择） |
|---|---|---|
| 位置 | 进程内，运行时加载 | 编译进引擎（与 gdscript/mono 同层） |
| API 访问 | ClassDB 白名单 | 全量头文件（含 EditorNode 内部） |
| 引擎改动 | 不可能 | 可以（headless 定制等） |
| 生命周期 | EditorPlugin 钩子 | `MODULE_INITIALIZATION_LEVEL_*` |
| 独立分发 | ✅（核心收益） | ❌ |
| 引擎升级兼容 | 二进制隔离（compat） | 随引擎重编译 |
| 上游合入冲突 | 少 | fork 负担（本项目已接受） |

**能力上：GDExtension ⊆ 模块（严格子集）**——GDExtension 能实现的模块都能；模块能实现的（特权部分）GDExtension 不一定能。GDExtension 唯一优势（独立分发）本项目用不上。

## 6. GDExtension 在什么场景才合适（客观判据）

- 给**第三方**分发扩展（社区插件生态，引擎版本解耦维护）；
- 需要**新类进 ClassDB** 供 GDScript/用户使用（自定义 Node/Resource）；
- 项目保持**引擎贴近上游**（不做深定制），自定义功能全部外挂；
- 复用 godot-cpp 社区生态。

本项目这三个场景都不成立（自用、TS 脚本用 fork 直注册、已在深定制），所以核心能力层选引擎模块；**GDExtension 不参与 Provider 的 Ops/Registry/Transport/Events 任何一层**。

---

## 附：证据索引

- GDExtension 官方定位：godot-docs `engine_details/engine_api/gdextension/`（C 接口/示例/文件格式）
- ClassDB 暴露面：`class_editorinterface.rst`（编辑器门面）；godot-docs/classes/ 无 `class_editor_node`
- EditorNode 内部：`editor/editor_node.cpp:8399`（cmdline_mode）；Ops 直取单例（ops.cpp:516,636）
- 语言注册：`modules/gdscript/register_types.cpp:144`；`core/object/script_language_extension.h:237`（ScriptLanguageExtension : ScriptLanguage）
- 生命周期：`modules/att_nodejs_sidecar/register_types.cpp`（EDITOR 级首帧）
- 调用链路与分层：`doc/plans/整体架构-Godot核心-ElectronUI-TSScript-设计方案.md`（§3.0 术语、§4.1 调用链路示例）
