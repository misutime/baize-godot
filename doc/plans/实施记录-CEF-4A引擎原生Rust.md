# 实施记录：CEF 4A 引擎原生 Rust 集成

> **用途**：4A 路线（《CEF集成-4A引擎原生Rust-方案.md》）的分阶段实施记录。与《实施记录-引擎级WebDock-RouteB.md》**分开维护**——本文只记 4A（crates/webview-core + C ABI + 剥离 gdext），RouteB 记录管 B 路线的 WebDock 功能。
>
> **对照方法**：每阶段给出"文件 → 变更 → 理由 → 验证"，与代码一一对应。

---

## 阶段 M0：脚手架（2026-08-02）

### 目标

建立"crates/ workspace + C ABI + SCons cargo 集成"的最小闭环：Rust 核心能编译、能链接进引擎、C++ 壳能创建/销毁/每帧 pump——**在接入任何 CEF 逻辑之前先证明构建与调用链路成立**。CEF 从 M1 起接入。

### 文件变更总览

| 文件 | 状态 | 作用 |
|---|---|---|
| `crates/Cargo.toml` | 新增 | Rust workspace 根（`members = ["webview-core"]`），单一 Cargo.lock |
| `crates/webview-core/Cargo.toml` | 新增 | crate 清单：`crate-type = ["staticlib"]`，edition 2024，MIT |
| `crates/webview-core/src/lib.rs` | 新增 | C ABI 桩：`wv_create` / `wv_destroy` / `wv_pump` |
| `modules/webview/webview_ffi.h` | 新增 | C ABI 契约头（C++ 壳与 Rust 唯一通信面） |
| `modules/webview/SCsub` | 修改 | 新增 SCons Command 调 cargo + LINKFLAGS 注入 |
| `modules/webview/webview_manager.h/cpp` | 修改 | 新增 Rust 核心生命周期（init/shutdown/pump）；**保留** gdext 加载（M1 前） |
| `modules/webview/register_types.cpp` | 修改 | SCENE 水位 init_core / uninit shutdown_core |
| `modules/webview/web_panel.cpp` | 修改 | `_process` 每帧驱动 pump |
| `.gitignore` | 修改 | 追加 `crates/target/`（cargo 产物，GB 级） |

### 逐文件详解（对照代码）

#### 1. `crates/webview-core/src/lib.rs`（Rust 核心骨架）

| 项 | 实现 | 说明 |
|---|---|---|
| `WebViewCore` | `#[repr(C)]` 结构体，字段 `_private: ()` | 不透明句柄；M1 起持有 CEF 初始化状态 |
| `WvCallbacks` | 三个 `Option<extern "C" fn>`：on_paint / on_message / on_load_status | C++ 侧回调（M1 接线，M0 传 nullptr） |
| `wv_create` | `Box::into_raw` 分配 + `eprintln!("[webview-core] wv_create (M0 skeleton)")` | 返回句柄；CEF 初始化 M1 接入 |
| `wv_destroy` | `Box::from_raw` 释放 + eprintln | 防 null；CEF 关闭 M1 接入 |
| `wv_pump` | 空函数 | M1 起接 `do_message_loop_work` |
| `#[unsafe(no_mangle)]` | 三处导出 | **edition 2024 要求**（`no_mangle` 升级为 unsafe，编译报错后修正） |

**边界纪律**（文件头注释）：本 crate 不依赖 Godot，只经 C ABI 通信。

#### 2. `modules/webview/webview_ffi.h`（契约头）

- 声明与 lib.rs 一一对应：`WebViewCore`、`WvCallbacks`、三个回调 typedef、三个导出函数
- `extern "C"` 守卫（C/C++ 双用）
- 文件头注释写明**边界纪律**：只承载浏览器语义，禁止 Godot 对象模型穿越（防退化 mini-gdext）

#### 3. `modules/webview/SCsub`（构建集成，关键机制）

```python
core_lib = env.Command(
    "#/crates/target/release/webview_core.lib",
    ["#/crates/Cargo.toml", "#/crates/webview-core/src/lib.rs"],
    "cargo build --release --manifest-path crates/Cargo.toml -p webview-core",
    chdir=Dir("#/"),
)
env.Append(LINKFLAGS=[core_lib[0]])
```

- **Command**：SCons 调 cargo 产出 staticlib；源依赖 = Cargo.toml + lib.rs（改 Rust 代码自动触发重编）
- **LINKFLAGS 注入共享 env 的 Node**：Node 参与链接命令行 → SCons 自动建立"链接依赖 lib"的关系
- **安全性**：模块仅编辑器构建（config.py `can_build` 门控）→ 模板/导出构建不含本模块 → SCsub 不执行 → 不污染其他链接

#### 4. `modules/webview/webview_manager.h/cpp`（生命周期）

| 函数 | 实现 | 调用时机 |
|---|---|---|
| `init_core()` | `wv_create(exe_dir.utf8().get_data(), nullptr, nullptr)` + 空指针 ERR_PRINT + 成功日志 | SCENE 初始化（register_types） |
| `shutdown_core()` | `wv_destroy(core)` + 置空 | SCENE 反初始化 |
| `pump()` | `wv_pump(core)`（core 非空时） | 每帧（WebPanel._process） |
| `load_cef_extension()` | **未变**（B-Host 遗留） | SCENE 初始化，M1 移除 |

**要点**：M0 双轨并存——gdext 加载（当前渲染依赖）+ Rust 核心（新底座），M1 渲染切换后删 gdext。

#### 5. `modules/webview/register_types.cpp`

- `initialize_webview_module(SCENE)`：`load_cef_extension()` → `init_core()`（顺序：先旧路径后新核心，互不干扰）
- `uninitialize_webview_module(SCENE)`：`shutdown_core()` → `free_singleton()`（先关核心再释放单例）

#### 6. `modules/webview/web_panel.cpp`

- `NOTIFICATION_READY`：追加 `set_process(true)`（启用每帧处理）
- 新增 `NOTIFICATION_PROCESS`：`WebViewManager::get_singleton()->pump()`（每帧驱动消息泵）
- 新增 include `webview_manager.h`

**pump 驱动者说明**：M0 由 dock 面板的 `_process` 驱动（面板存在才 pump）；dock 关闭时无 pump——M0 可接受（CEF 未初始化），M1 定夺 pump 归属（可能需要编辑器级驱动而非面板级）。

### 验证状态

| 项 | 结果 |
|---|---|
| cargo 编译 | ✅ `webview_core.lib` 产出（edition 2024 `unsafe(no_mangle)` 修正后） |
| `task dev` 全量构建 | ✅ 21.2s——SCons 输出可见 `cargo build --release --target x86_64-pc-windows-msvc`，LIBS 链接成功 |
| 运行时冒烟 | ✅ `just b0-load`：`[webview-core] wv_create (M0 skeleton)` + `[WebView] Rust core created (4A M0).`；编辑器正常运行（每帧 pump 空转无崩溃）；gdext 路径照旧渲染 |
| 退出销毁 | ⚠️ 代码路径存在（uninit SCENE → shutdown_core → wv_destroy）；本次粘贴未见日志（可能强杀/截断），正常关闭应出现 `wv_destroy` |

### 代码审查（reviewer，2026-08-02）

**结论**：3×P1 + 1×P2，修复后通过。

| 严重度 | 发现 | 修复 |
|---|---|---|
| P1 | SCsub 硬编码 MSVC `.lib` + 默认 host triple（Linux/macOS 产出 `.a`、交叉编译架构错） | 目标感知：按 `platform/arch/mingw` 推导 `rust_target` + artifact 名，`cargo --target` 显式传；不支持目标显式报错 |
| P1 | 静态库放 LINKFLAGS → 单遍链接器先于目标文件扫描 → `wv_*` 符号解析不到 | 改 `LIBS`（排在目标文件后），Node 依赖保留 |
| P1 | Command 只跟踪 lib.rs + workspace 清单（改 manifest/lock 后静默链接旧产物） | 输入全量跟踪：workspace 清单 + Cargo.lock + crate 清单 + `Glob(src/*.rs)` |
| P2 | `eprintln!` 在 extern "C" 边界可能 panic（stderr 写失败）→ 中止进程 | 非 panic 的 `log_stderr`（忽略写错误） |
| 建议 | Cargo.lock 应提交 | ✅ 保持未忽略，纳入提交 |

### 遗留 / 下一步

- [x] M0 运行时冒烟确认（创建/pump 无崩溃；销毁路径待正常关闭时复核日志）
- [ ] 正常关闭编辑器时确认 `wv_destroy` 日志出现
- [ ] M1：CEF 初始化接入（wv_create 内 cef 初始化）+ `WvOnPaint` 回调 → ImageTexture → WebPanel 渲染（替换 gdext 路径）
- [ ] M1 后移除 gdext：`load_cef_extension` 调用、bin/webview/ 中 gdext 产物
- [ ] pump 归属决策（面板级 vs 编辑器级）
