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

- 构建：`task dev`（-j20 + `debug_symbols=no`）
- 测试 1（惰性态，不设环境变量）：预期 `[WebView] CEF extension load skipped (GODOT_CEF_EXTENSION not set).` + 编辑器正常打开
- 测试 2（加载态，设 `GODOT_CEF_EXTENSION` 指向自编译 addon 的 `godot_cef.gdextension`）：预期三行关键日志
  ```
  [WebView] Loading CEF extension: <path>
  Initialize godot-rust (API v4.5.stable.official, runtime v4.8.dev.custom_build, safeguards balanced)
  [WebView] CEF extension loaded OK.
  ```
- **通过标准**：godot-rust 初始化行 + `loaded OK` + 编辑器正常打开无报错

### 遗留问题 / 待办

- [ ] 若 descriptor 的 `bin/...` 相对路径按 res:// 解析（而非 descriptor 所在目录），dll 将找不到——届时把 addon 拷入测试项目改用 res:// 路径重测
- [x] 代码评审（reviewer）结论落地：**正确，无 finding，可提交**（置信度 0.98，2026-08-02）
- [ ] 环境变量门控是 B0 临时机制；MVP1 改为 exe 相对分发目录加载（`<exe>/../webview/`）

### 下一步（MVP1）

`modules/webview/` 扩展：`WebPanel : Control` 封装（url / send_message / on_message）+ `editor_web_dock`（`add_control_to_dock` LEFT_UL）→ 编辑器打开任意项目可见可停靠的 web dock（先加载 smoke 的 `bridge.html`）。
