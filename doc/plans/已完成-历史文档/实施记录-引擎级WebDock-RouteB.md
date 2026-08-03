# 实施记录：引擎级 WebDock（Route B）

> **用途**：按阶段记录 Route B 的实施过程——每个阶段的目标、核心文件与函数、验证结果、遗留问题。与方案文档（`Godot编辑器UI重构方案-TS路线-引擎级WebDock-RouteB-方案.md`）配套：方案文档定义"做什么/怎么做"，本文记录"做了什么/结果如何"。
>
> 每次阶段完成追加一节，不回改历史记录；修订只以追加批注形式。

---

## 阶段 B0：模块内加载 gdcef 扩展（2026-08-02）

### 阶段目标

验证引擎模块在编辑器启动时序内调用 `GDExtensionManager::load_extension()` 加载 gdcef（CEF GDExtension）可行、无冲突。这是 Route B 的承重前置——MVP 的 WebDock 依赖 `CefTexture` 类在编辑器进程内注册。

### 核心文件与功能

| 文件 | 功能 |
|---|---|
| `modules/webview/config.py` | 模块构建声明：`can_build` 仅编辑器构建（`env.editor_build`）；自动发现走 `methods.detect_modules`（SConstruct:459），**无需改 SConstruct** |
| `modules/webview/SCsub` | 构建脚本：`env_modules.Clone()` + `add_source_files(env.modules_sources, "*.cpp")` |
| `modules/webview/register_types.h` | 声明 `initialize_webview_module` / `uninitialize_webview_module`（4.8 模块 level 制，由生成的 `register_module_types.gen.cpp` 调用） |
| `modules/webview/register_types.cpp` | **SCENE 水位**触发 `WebViewManager::load_cef_extension_if_requested()`；`uninitialize` 时释放单例 |
| `modules/webview/webview_manager.h` | `WebViewManager` 单例声明（B0 阶段纯 C++ 单例，未注册为脚本类） |
| `modules/webview/webview_manager.cpp` | 读 `GODOT_CEF_EXTENSION` 环境变量 → `GDExtensionManager::load_extension()` → 打印加载状态 |

### 关键函数职责

| 函数 | 职责 |
|---|---|
| `initialize_webview_module(ModuleInitializationLevel p_level)` | 模块初始化入口；`p_level == SCENE` 时触发扩展加载 |
| `uninitialize_webview_module(ModuleInitializationLevel p_level)` | 模块卸载入口；SCENE 水位释放单例 |
| `WebViewManager::get_singleton()` / `free_singleton()` | 单例生命周期（`memnew`/`memdelete`） |
| `WebViewManager::load_cef_extension_if_requested()` | ①环境变量未设 → 打印一行可观测日志后惰性返回（**不静默**）；②已设 → `load_extension` 并按 `LoadStatus` 上报：OK / ALREADY_LOADED / NEEDS_RESTART / FAILED(ERR_PRINT) |

### 设计依据（已核实的事实）

- `GDExtensionManager::_load_extension_internal` 是 **level 感知**的：任何时机加载都会补初始化到当前水位（`level >= 0` 分支循环 `initialize_library`），SCENE 之后的 EDITOR 水位由 main.cpp 的 `initialize_extensions(LEVEL_EDITOR)` 接管；
- 项目扩展列表（`.godot/extension_list.cfg`）在 `register_core_types()`（main.cpp:696）就加载——**早于**模块 level 初始化，因此 B0 测试项目必须**不带 addon**，避免 ALREADY_LOADED 混淆；
- 模块 level 初始化：`initialize_modules(MODULE_INITIALIZATION_LEVEL_SCENE)`（main.cpp:773）在扩展 SCENE 初始化（:774）之前。

### 验证结果

**✅ 全部通过（2026-08-02）**——三态实测：

| 测试 | 命令 | 关键输出 | 结论 |
|---|---|---|---|
| 加载+类注册（无头） | `just b0-check` | `[WebView] CEF extension loaded OK.` + `CefTexture registered: true` | 模块加载的扩展类注册成功 |
| 惰性态（编辑器） | `just b0-inert` | `[WebView] CEF extension load skipped (GODOT_CEF_EXTENSION not set).` + 编辑器正常启动 | 惰性可观测，不静默 |
| 加载态（编辑器） | `just b0-load` | `Loading CEF extension` → `Initialize godot-rust (API v4.5, runtime 4.8.dev)` → `loaded OK` + Vulkan hook 装上 | 引擎级加载无冲突 |

- 构建期间发现并修复：`webview_manager.h` 错误 include `core/string/string.h` → 改为 `core/string/ustring.h`（Godot 4 的 String 头）
- 新增 `justfile`（测试命令入口：dev / b0-inert / b0-load / b0-check / gdcef-build），不影响 Taskfile 构建
- 附带观察：Vulkan loader 的 registry lookup WARNING 为环境问题（缺 layer manifest 注册表项），与 webview 无关

### 遗留问题 / 待办

- [ ] 若 descriptor 的 `bin/...` 相对路径按 res:// 解析（而非 descriptor 所在目录），dll 将找不到——届时把 addon 拷入测试项目改用 res:// 路径重测
- [x] 代码评审（reviewer）结论落地：**正确，无 finding，可提交**（置信度 0.98，2026-08-02）
- [x] B0 三态验证通过（见上）
- [x] 环境变量门控是 B0 临时机制；MVP1 改为 exe 相对分发目录加载（`<exe>/../webview/`）

---

## 阶段 MVP1：WebPanel + 编辑器 WebDock（代码完成，待验证）

### 阶段目标

编辑器打开任意项目 → 左侧出现可停靠的 WebDock（React/HTML 页面），页面/扩展均随编辑器分发（`bin/webview/`），与打开的项目无关。

### 核心文件与功能

| 文件 | 功能 |
|---|---|
| `modules/webview/web_panel.h/cpp` | **WebPanel : Control**（GDCLASS）——`url` 属性 / `send_message` / `on_message` 信号；内部经 `ClassDB::instantiate("CefTexture")` 创建扩展类实例，Object API 交互；NOTIFICATION_READY 时创建并连 `ipc_message` 信号 |
| `modules/webview/editor_web_dock.h/cpp` | **WebDockPlugin : EditorPlugin**——创建 `EditorDock`（标题 WebDock，LEFT_UL，可关闭），承载 WebPanel；`register_web_dock_deferred` 自由函数供 MessageQueue 第一帧延迟注册 |
| `modules/webview/register_types.cpp` | SCENE: GDREGISTER WebPanel + 加载扩展；EDITOR: `MessageQueue::push_callable(callable_mp_static(register_web_dock_deferred))` |
| `misc/scripts/stage_webview.py` | 产物暂存脚本——路径直接写死（`../refers/godot-cef/addons/godot_cef` → `bin/webview/`），缺失报错退出，写 MANIFEST 清单 |
| `Taskfile.yml` / `justfile` | `stage-webview` 任务（单一暂存入口）；测试配方（dev/webview-stage/b0-load/b0-check） |

### 关键设计（已修正）

- **分发与项目无关**：扩展从 `<exe_dir>/webview/godot_cef.gdextension` 自动加载（不再用环境变量）；页面经 `file:///<exe_dir>/webview/ui/bridge.html` 加载（不用 res:// 项目域）——回应"编辑器 UI 与具体项目无关"的正确架构；
- **集成模型：产物关联、源码独立**——godot-cef 源码不进入本仓库，只消费编译产物（addons/godot_cef），构建/分发时 `task stage-webview` 自动附带；`bin/` 已 gitignore（.gitignore:267），100MB+ 产物不进 git。

### Fork 特有 API 发现（编译期实测，重要）

1. **Node 生命周期 hook 非虚函数**：本 fork 的 `_enter_tree`/`_exit_tree` 为 GDVIRTUAL-only；`_notification` 非虚，但经 **GDCLASS 宏生成的 `_notification_forwardv` 编译期分发链**派发——子类定义 `void _notification(int)` 即可收到通知，**不能加 `override` 关键字**（C3668 错误）；
2. **`EditorPlugin::add_control_to_dock` 已 deprecated**：现代 API = `EditorDock`（`set_title`/`set_default_slot`/`set_closable`）+ `EditorPlugin::add_dock(EditorDock*)`（内部走 EditorDockManager）；
3. `callable_mp` 需显式 include `core/object/callable_mp.h`。

### 验证状态

- ✅ 构建通过（2026-08-02，16.7s：webview 三文件编译 + 全量链接）
- ⏳ 功能验证待跑：`just b0-load` → 左侧 WebDock 渲染 bridge.html
- 验收：WebDock 出现可拖拽停靠、页面渲染可交互、控制台 `WebDock registered` 日志

### 空面板根因（shifu 排查，2026-08-02）

**根因**：`CefTexture` 是**非 tool 类**（`#[class(base=TextureRect)]`，无 `tool` 修饰，cef_texture/mod.rs:22-24）；gdext 0.5.3 默认 `EditorRunBehavior::ToolClassesOnly`（godot-core-0.5.3 init/mod.rs:477-480）。编辑器进程里 `ClassDB::instantiate("CefTexture")` 返回**占位对象**（class_db.cpp:609-636），其通知回调为 no-op（class_db.cpp:150-152）→ Rust 的 READY/PROCESS 永不运行 → CEF 惰性初始化（`cef_retain` 在 `on_ready`，cef_init.rs:61-75）从未触发 → 无浏览器、无加载信号、纹理空白。

**修复**：`web_panel.cpp` 改用 `ClassDB::instantiate_no_placeholders(SNAME("CefTexture"))`（class_db.h:346），强制真实类实例化。

**对比证据**：godot-cef 的 `CefTexture2D` 显式带 `tool`（cef_texture2d/mod.rs:49-51）——兄弟类特意开启编辑器执行，CefTexture 没有。上游侧替代方案（给 CefTexture 加 `tool`）不采用：保持 godot-cef 源码独立。

**连带发现**："GDExtension 加载 OK + Vulkan hook" 不等于 CEF 已初始化——CEF 是首个真实浏览器对象创建时才 `cef_retain()` 惰性初始化（lib.rs:28-46 只装 hook/权限）。

**同轮伴生修复（保留）**：`web_panel` 的 `SIZE_EXPAND_FILL` + `custom_minimum_size(320x240)` 不是根因修复，但是布局必需——MarginContainer 内 Control 无展开标志会塌缩为 0×0（即使浏览器真实存在也渲染不出）；load_finished/load_error 日志为永久可观测性。

### MVP1 验证完成（2026-08-02）

**✅ 通过**：编辑器打开任意项目 → WebDock 渲染 bridge.html（`page loaded ... status 200` + `Creating browser in software rendering mode`）；可拖拽停靠、可关闭。

**控制台错误全部定性**：

| 错误 | 性质 | 处置 |
|---|---|---|
| `vkGetMemoryWin32HandlePropertiesKHR` 加载失败 → 加速 OSR 回退软件 | **预期**（V1 软件渲染先行；fork Vulkan 驱动未暴露该函数） | 不阻塞；V2 GPU 走 D3D12 分支（fork 支持，E0 已核实） |
| `Invalid UTF-16 string` | **良性噪音**：cef crate（依赖 148.4.0）`string.rs:508` 在 null CEF 字符串转换时 `eprintln!`；不 fork 依赖无法消除 | 忽略 |
| chrome extension.crx（QQPCMgr） | 环境噪音：Chromium 读注册表外部扩展条目 | 忽略 |
| DevTools `ws://127.0.0.1:9229` | 彩蛋：远程调试端口，React 调试可用 | 保留 |

**MVP1 验收清单**：WebDock 出现/拖拽/关闭 ✅；页面渲染可交互 ✅；扩展自动加载与项目无关 ✅。

---

## 决策：转向 4A 引擎原生 Rust 集成（2026-08-02）

**裁决**：用户拍板走 **4A 路线**——不修改 refers/godot-cef 源码；新建 Rust 工程（vendor CEF 通用层），重写 Godot 连接层为自有 C ABI，Rust 核心以 staticlib 融入引擎模块，**剥离 gdext**。

**触发动因**（B-Host 四接缝）：① tool 语义（占位对象 workaround）；② 页面 scheme（file:// 受限）；③ **gdext 版本缝**（api-4-5 vs 4.8-dev，无官方 api-4-8，引擎升级要等跟进）；④ 类型缝（字符串 API）。4A 结构性消除 ①③④，② 引擎原生解决。

**关键实测依据**：cef_app（1689 LOC）与 software_render（206 LOC）**0 文件 import godot**——CEF 通用层可原样 vendor；gdcef（16062 LOC，35/47 godot 耦合）为唯一重写对象。

**方向说明**：4A = 《RouteB-方案.md》§7 演进路径（B-RustA）的具体化定案；M1 渲染/桥/分发沿用已验证的 WebPanel/WebDock/分发模型，仅驱动层从 Object API 改 C ABI。权威文档：《CEF集成-4A引擎原生Rust-方案.md》。

**边界纪律**：C ABI 只承载浏览器语义（lifecycle/paint/message/input/ime），禁止 Godot 对象模型穿越（防退化 mini-gdext）。

### 下一步（MVP2）

双向桥：`EditorSelection.selection_changed` 信号 + `_process` 帧轮询 diff → 推 `selection_changed`/`position_changed`；收 `set_prop` → `EditorUndoRedoManager` 入栈 → `set_position`。验收：选 Node3D 显示 X；改数字节点动可撤销；3D 拖动数字跟随。

### 下一步（MVP1）

`modules/webview/` 扩展：`WebPanel : Control` 封装（url / send_message / on_message）+ `editor_web_dock`（`add_control_to_dock` LEFT_UL）→ 编辑器打开任意项目可见可停靠的 web dock（先加载 smoke 的 `bridge.html`）。
