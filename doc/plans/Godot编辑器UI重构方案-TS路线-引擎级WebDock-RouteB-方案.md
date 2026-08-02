# Godot 编辑器 UI 重构（TS 路线）——Route B 方案：引擎级 WebDock（MVP 版）

> **定位**：本文件是《Godot编辑器UI重构方案-TS-CEF嵌入与NodeSidecar-设计.md》的引擎级内嵌实施方案（2026-08-02 编写，**以 MVP 方案与架构为准**，与本文件早期草稿冲突处已按 MVP 修订）。**用户裁决**：编辑器 UI 采用 **Route B——引擎层面内嵌**（web dock 随 fork 编辑器分发），不走项目级 EditorPlugin 路线。E0 已验证的底层能力（OSR 渲染/IME/桥/4.8-dev 兼容）全部迁移到本方案。
>
> **证据标注**：API 与仓库事实标注来源；推断标 `[INFERENCE]`。引用《设计》= TS-CEF 方案设计文档。

---

## 1. 目标

**MVP 目标（用户定义）**：创建一个 WebDock——像 FileSystem/Scene/Inspector 一样的可停靠面板（可拖拽到左侧/右侧/底部），内部是 **React 网页**。页面上有一个 **Node3D 的 Position X 数字**：

1. 该数字与实际**选中节点**的位置关联；
2. 修改数字 → **C++ 3D 视口内的节点跟着移动**（可撤销）；
3. 在 3D 视口内拖动节点 → **页面数字跟着变**（双向联动）。

**长期目标**：以此为第一块面板，把编辑器 UI 渐进式替换为 React/HTML——打包后游戏开发者操作的编辑器自带网页面板，不依赖项目安装插件。

## 2. MVP 决策（按目标反推，已定）

| # | 决策 | 结论 | 理由（锚定 MVP） |
|---|---|---|---|
| 1 | 引擎模块形态 | **B-Host：`modules/webview/` C++ 模块托管已验证的 gdcef 扩展** | MVP 无 GPU 需求、无 gdext 版本滞后痛点 → 无演进触发条件；托管起步最快到"引擎级 web dock"，godot-cef 保持上游跟踪 |
| 2 | WebDock 开启策略 | **默认开**，仅 `TOOLS_ENABLED`（编辑器构建）激活 | MVP 价值 = 编辑器里可见可用；默认关则无法验收 |
| 3 | 面板封装 | **薄 C++ 封装 `WebPanel : Control` + 统一桥协议** | 桥逻辑（选中/属性双向 + undo）必须在引擎侧集中一处，未来每个面板复用 |
| 4 | 页面分发 | **exe 相对目录 `webview/ui/` + `file://` 加载 + Vite `base:'./'`** | `res://` 是项目域装不了编辑器自带页面；MVP 无需自定义 scheme [INFERENCE] |
| 5 | 属性双向机制 | **`selection_changed` 信号 + `_process` 帧轮询 diff** | 编辑器无 per-property 信号；轮询只在变化时推送（阈值），符合《设计》"增量 diff"原则 |
| 6 | 撤销 | **`EditorUndoRedoManager`**（编辑器 undo 栈，非游戏侧 UndoRedo） | set_prop 不入 undo 栈则撤销语义破，MVP 必须带 |

## 3. 架构

```
fork 引擎 (baize-godot)
├── modules/webview/                    ← 新增引擎模块（C++，SCons，本 fork 维护）
│   ├── webview_manager.cpp/h           ← 单例：启动时 GDExtensionManager::load_extension() 加载 gdcef
│   ├── web_panel.cpp/h                 ← WebPanel : Control（统一 API，内部封装 CefTexture）
│   ├── editor_web_dock.cpp/h           ← 编辑器 WebDock（add_control_to_dock，LEFT_UL 可拖拽停靠）
│   └── web_bridge.cpp/h                ← 桥协议处理（选中/属性双向 + undo 入栈）
├── editor/                             ← 不改核心，仅编辑器初始化时挂 dock
└── 分发数据目录: <exe>/../webview/      ← gdcef 扩展 + CEF 运行时 + ui/ 页面产物
```

**三层分工**：

| 层 | 职责 | 语言/来源 |
|---|---|---|
| 引擎模块 | 扩展加载、WebPanel 封装、dock 接入、桥协议 | C++（薄壳，本 fork 维护） |
| CEF 核心 | 浏览器/OSR/IPC/IME 全逻辑（含通信原语） | **Rust，godot-cef 原样**（不 fork，上游跟踪） |
| 页面层 | React UI（Position X 输入） | TS，ui/ 工程（Vite，`base:'./'`） |

**WebPanel 统一 API**（对应《设计》§3.3）：

```
WebPanel (Godot Control) { url: String; send_message(json: String); on_message(信号: String) }
```

**关键接入点（API 已核实）**：
- 扩展加载：`GDExtensionManager::load_extension()`（core/extension/gdextension_manager.h:71）；`CefTexture` 经 ClassDB 实例化
- dock 接入：`EditorPlugin::add_control_to_dock(DockSlot, Control*)`（editor/plugins/editor_plugin.h:153）
- 选中监听：`EditorInterface::get_singleton()->get_selection()` → `EditorSelection`（editor/editor_data.h:276），`selection_changed` 信号 + `get_selected_nodes()`
- 撤销：`EditorUndoRedoManager::get_singleton()`（editor/editor_undo_redo_manager.h:148），`create_action` / `add_do_method` / `add_undo_method`

## 4. 通信桥（重点）

### 4.1 物理链路：CEF 多进程下的三段

CEF 是多进程架构，web 页面运行在**独立的 renderer 进程**，所以"web ↔ Godot 通信"跨两段：

```
┌─ Godot 进程 ─────────────────────────────────────────┐
│  WebPanel (C++)                gdcef (Rust)          │
│  on_message 信号 ──┐          browser_process.rs     │
│                    │  ① 进程内调用（同进程，直接函数调用）│
│  send_message ─────┘  ←── CefProcessMessage 路由     │
└──────────────────────┬───────────────────────────────┘
                       │ ② CEF 进程间消息（OS 级 IPC，Chromium 内建）
┌──────────────────────▼───────────────────────────────┐
│ renderer 进程（独立 OS 进程）                          │
│  v8_handlers.rs 注入 JS 原语：                        │
│    window.sendIpcMessage / window.onIpcMessage       │
│    window.ipcMessage.addListener(...)                │
│  React 页面                                           │
└──────────────────────────────────────────────────────┘
```

- **① Godot ↔ CEF browser 段**：扩展加载进 Godot 进程，`ipc_message` 信号/`send_ipc_message` 方法 = 进程内直接调用（E0 已验证）；
- **② browser ↔ renderer 段**：`CefProcessMessage`（Chromium 内建 IPC），对宿主透明；
- **③ JS 侧绑定**：V8 handler（`crates/cef_app/src/v8_handlers.rs`）注入 `window.sendIpcMessage` 等原语，消息契约在 `ipc_contract.rs`，路由在 `render_process.rs`/`browser_process.rs`（源码已核实）。

> 与《设计》§4"CEF↔Godot 进程内桥（CBOR IPC）"的关系：**同源复用**——gdcef 就是该进程内桥的现成实现，无需自研；崩溃隔离因此天然成立（renderer/GPU 进程崩溃不带走 Godot，§5.4）。

### 4.2 gdcef 通信原语（E0 已实测）

| 方向 | JS 侧 | Godot 侧 | 传输 |
|---|---|---|---|
| JS → Godot | `window.sendIpcMessage(msg)` | `CefTexture.ipc_message` 信号 | String（JSON） |
| Godot → JS | `window.onIpcMessage(msg)` | `cef.send_ipc_message(msg)` | String（JSON） |
| 类型化（可选） | `window.sendIpcData(v)` / `window.ipcDataMessage` | `ipc_data_message` 信号 / `send_ipc_data()` | CBOR（Variant ↔ JS 值） |

**MVP 主通道 = String + JSON**（与《设计》§4"统一 JSON-RPC 2.0 语义"一致）；CBOR 通道留待类型化大载荷场景启用。

### 4.3 桥协议（JSON-RPC 子集，MVP 共 3 条消息）

**web → engine**：

```
{"cmd":"set_prop","path":"position:x","value":1.5}
```
→ engine：取当前选中节点 → `EditorUndoRedoManager` 入栈（undo 恢复旧值）→ `node->set_position(...)`。

**engine → web**：

```
{"event":"selection_changed","position":[x,y,z]}   // 选中变化时推送（含未选中 null）
{"event":"position_changed","position":[x,y,z]}    // 选中节点位置变化时推送（帧轮询 diff）
```

**数据流（双向）**：
- **UI → 引擎**：React 输入框 change → `window.sendIpcMessage(set_prop)` → ① 进程内 → web_bridge → undo 入栈 → `set_position` → 3D 视口节点移动；
- **引擎 → UI**：3D 视口拖动节点（编辑器 gizmo 改 position）→ web_bridge 的 `_process` 帧轮询检测到变化（阈值 `1e-6`，节流只在变化时发）→ `send_ipc_message(position_changed)` → React 更新数字；场景树选中变化 → `EditorSelection.selection_changed` 信号 → 同样推送。

### 4.4 语义约束（《设计》§4 三条约束的 MVP 落法）

1. **Godot 唯一状态权威**：position 只存于引擎侧节点；web 只发命令、不缓存副本作为权威，收到事件才更新显示；
2. **连接与生命周期由 Godot 管**：模块加载扩展、建 dock、管 WebPanel 生命周期；数据面直连（进程内桥）；
3. **统一消息语义**：三端（本 MVP 两端）共用上述 JSON 消息格式，类型漂移靠文档约束（CI 校验留待 M4）。

## 5. MVP 实施路径

```
B0 前置验证（半天）: 模块内 GDExtensionManager::load_extension() 时序
                     验收: 编辑器启动加载 gdcef 无冲突（godot-rust 初始化行正常、无报错）
MVP1（骨架+静态页）: modules/webview/ SCsub+register_types + webview_manager + web_panel
                     + editor_web_dock（DOCK_SLOT_LEFT_UL，先加载 smoke 的 bridge.html）
                     验收: 编辑器打开任意项目 → WebDock 渲染页面可交互、可拖拽停靠
MVP2（双向桥+undo）: web_bridge（set_prop→EditorUndoRedoManager；selection_changed 信号
                     + _process 帧轮询 diff→position_changed）
                     验收: 选 Node3D 显示 X；改数字节点移动可撤销；3D 拖动数字跟随
MVP3（React 壳）   : ui/ 工程（Vite base:'./'）→ 产物进 <exe>/../webview/ui/
                     验收: dock 内是 React 应用，MVP2 功能在 React 壳上全过
```

**MVP 验收（四条，对应目标）**：

| # | 验收项 | 判定 |
|---|---|---|
| 1 | 编辑器打开任意项目 → 左侧 WebDock，可拖到右侧/底部停靠 | 页面渲染、拖拽停靠正常 |
| 2 | 场景选中 Node3D → 页面显示其 Position X | 数字与选中节点一致 |
| 3 | 页面改 X → 3D 视口节点移动 | 移动生效，Ctrl+Z 撤销恢复 |
| 4 | 3D 视口拖动节点 → 页面 X 实时跟随 | 数字随拖动更新 |

## 6. 后续里程碑（MVP 之后）

- **M4 Node sidecar 三端直连**：CEF↔Godot 进程内桥 + CEF↔NodeJS WebSocket + NodeJS↔Godot WebSocket（《设计》§4）；
- **M5 Inspector 迁移**：类型清单驱动属性面板（《设计》§5.7），第一个正式替换面板；
- **V2 GPU 加速**：D3D12 分支（fork Windows 默认渲染器 = d3d12，E0 已核实；需 4.6 beta2+，fork 4.8 ✓），解决高频面板滚动/动画性能。

## 7. 演进路径（B-Host → B-RustA）

| 阶段 | 形态 | 动机 | 代价 |
|---|---|---|---|
| **MVP 起步（当前）** | C++ 模块托管 gdcef 扩展 | 最快到引擎级 web dock；上游跟踪保持 | 依赖 GDExtension 机制（内部细节，产品侧不可见） |
| **演进（按需）** | godot-cef Rust 核心以 staticlib 融入模块（剥离 gdext，C ABI 边界） | 托管成为瓶颈时：gdext 版本滞后、GPU 路径受限、产品化 | **fork godot-cef**（失去上游跟踪）；数周~月级工程 |

**触发条件**（任一出现才做）：① fork 升级时 gdext api-4-8 持续滞后；② GPU 加速在托管形态下无法与 RenderingDevice 充分集成；③ 产品化要求去除扩展加载机制。**当前不满足，MVP 形态即长期形态 [INFERENCE]**。

## 8. 构建与分发

- 引擎侧：`modules/webview/` 进 SConstruct（fork 无 unity/lld 选项，增量按文件，可接受）；
- 扩展侧：gdcef 构建仍走 godot-cef 的 `cargo xtask bundle`（Rust 工具链不变）；**产物拷入 `<exe>/../webview/`**（gdcef.dll + CEF 运行时 18 项 ~100MB）；
- 页面侧：ui/ 工程 `vite build --base=./` 产物进 `<exe>/../webview/ui/`（file:// 加载，相对路径资源）；
- Taskfile 增加：`webview-build`（gdcef 构建+拷贝）、`webview-ui`（页面构建+拷贝）；
- **版本锁定**：CEF 148.0.10（已裁决）；gdext api-4-5 锁定，fork 升级时重验（E0 已验证 4.8-dev 兼容）。

## 9. 风险清单

| 风险 | 说明 | 对策 |
|---|---|---|
| 模块内 load_extension 时序 | 编辑器启动时加载扩展与 ClassDB/编辑器初始化的先后 | B0 前置验证；失败则延迟到 EditorNode ready 后加载 |
| 帧轮询节流 | 每帧 diff 的开销与事件风暴 | 阈值 + 只在变化时推送（MVP 单节点无压力）；高频场景留 M5 |
| 编辑器输入焦点 | dock 点击/键盘与编辑器全局快捷键冲突（《设计》§5.2） | MVP1 实测；焦点表与快捷键治理 |
| dock 浮窗/多窗口/布局 | dock 拖出独立窗口时 CEF 纹理同步 | MVP1 布局测试 |
| file:// 限制 | file:// 页面 fetch/XHR 受限、Vite 产物路径 | 桥走 IPC 不走 HTTP；`base:'./'`；必要时再上自定义 scheme |
| gdext 版本滞后 | api-4-5 二进制 + fork 升级需重验/重编 | 升级流程 = 重编 godot-cef + E0 冒烟回归 |
| 分发路径 | exe 相对路径在不同安装形态下失效 | 分发目录与 exe 同目录规则；路径解析失败可观测（不静默） |
| 崩溃隔离 | CEF 子进程隔离；主库 in-process（同 GDExtension 结论） | no-panic 策略沿用 godot-cef；退出钩子顺序 |

## 10. 与既有裁决的关系（口径修订）

| 既有裁决 | 修订 |
|---|---|
| 计划文档 §9：webview 保持 GDExtension 形态 | **修订**：webview = 引擎模块（`modules/webview/`）托管 gdcef 扩展；GDExtension 是 CEF 载体，非最终形态（编辑器 UI 必须随编辑器分发） |
| 《设计》§3.3：融合方式（暂定保持 GDExtension） | **修订**：Route B——C++ 薄壳 + 托管扩展（MVP）；Rust 核心 staticlib 融合为演进路径（§7） |
| §9 其余（新原生组件 GDExtension+Rust、核心不替换） | **不变**——游戏内嵌/工具场景继续纯 GDExtension；引擎核心不替换原则不受影响 |

## 11. 待核实项

- **B0**：模块内 `load_extension` 在编辑器启动时序下的行为（最高优先，MVP 前置）
- 编辑器多窗口（dock 浮窗）下 CEF 纹理与输入行为
- 编辑器快捷键与 CEF 页面的按键冲突清单
- `file://` + Vite `base:'./'` 产物在 CEF 中的加载验证（MVP3 前置）
- Node sidecar 与编辑器进程的生命周期绑定（M4 前）
