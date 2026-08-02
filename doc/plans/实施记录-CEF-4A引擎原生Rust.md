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
- [x] M1a：vendor CEF 通用层 + 依赖升级（见下）
- [ ] M1b：CEF 初始化接入（wv_create 内 cef 初始化）+ 浏览器创建 + `WvOnPaint` 回调 → ImageTexture → WebPanel 渲染（替换 gdext 路径）
- [ ] M1 后移除 gdext：`load_cef_extension` 调用、bin/webview/ 中 gdext 产物
- [ ] pump 归属决策（面板级 vs 编辑器级）

---

## 阶段 M1a：vendor CEF 通用层 + 依赖升级（2026-08-02）

### 目标

把 godot-cef 的 CEF 通用层（cef_app / software_render）搬进 crates/，依赖升级到最新稳定（cef 151.x），并打通"文件配置 CEF 分发包"的构建链路。

### 文件变更

| 文件 | 变更 |
|---|---|
| `crates/cef-app/`（新 crate） | vendor 自 godot-cef 的 `crates/cef_app`（9 rs + ime_helper.js，MIT）；上游 Cargo.toml 依赖 cef 148.2.0 → **cef 151.1.0**、ciborium 0.2.2 |
| `crates/webview-core/src/software_render/` | vendor（206 LOC，mod.rs）；dead_code 警告暂保留（M1 使用） |
| `crates/webview-core/src/helper_main.rs` | CEF 子进程入口（基于 gdcef_helper 改写，去 gdext/mac；bin 名 webview-helper） |
| `crates/webview-core/Cargo.toml` | deps：cef-app(path) + cef 151.1.0 + cef-dll-sys 151.1.0（**不开 accelerated_osr**——M1 软件渲染省 wgpu/windows/objc2-metal）；`[[bin]] webview-helper` |
| `crates/cef-dist.txt`（新） | **固定文件配置**：CEF 分发包根目录（仓库外，二进制不进 git）；SCsub 读它 → CEF_PATH → cef-dll-sys build.rs 自动下载/定位 |
| `modules/webview/SCsub` | Command env 注入 CEF_PATH（克隆环境 + ENV 更新，跳过 # 注释行解析）；输入全量跟踪（双 crate 清单 + Cargo.lock + src 全 Glob） |

### 关键决策与坑

- **依赖升级**：用户裁决"最新稳定"——cef/cef-dll-sys 148.2.0 → **151.1.0+151.3.12**（CEF 分发包 151.3.12 自动下载到 `D:\misutime\104_game\cef-dist\151.3.12\`）；vendor 的 cef_app 代码（148 基准）对 151 API 编译兼容；
- **crate 结构镜像上游**：cef-app 独立 crate（helper 以 `cef_app::` 引用），软件渲染为 webview-core 模块；
- **坑**：① edition 2024 `#[unsafe(no_mangle)]`；② `ime_helper.js` 缺失（include_str!）；③ semver 版本要求去掉 `+metadata`（cargo 忽略，告警）；④ **SCsub 首行解析 bug**——cef-dist.txt 首行是注释，曾把注释当 CEF_PATH 传入（cmake 报错），改为跳过 `#` 行；⑤ SCons Command 的 `env=` 参数是替换构建环境不是加变量——用克隆 env + `ENV` 更新。

### 验证状态

- ✅ `cargo build`：cef-app + webview-core + webview-helper 全编译（software_render dead_code 3 条警告为预期）
- ✅ `task dev` 全量：cargo 经 SCons 调用（CEF_PATH 来自文件配置），CEF 151.3.12 下载成功，链接通过（46.4s）
- ⏳ 运行时：wv_create 仍是 M0 桩（CEF 未初始化），等 M1b

---

## 阶段 M1b：CEF 初始化 + OSR 软件渲染（2026-08-02）

### 目标

Rust 核心真正驱动 CEF：惰性初始化、OSR 浏览器创建、软件渲染 paint → C ABI 回调 → C++ 纹理上传。**gdext 路径移除**（load_cef_extension 删除）。

### 文件变更

| 文件 | 变更 |
|---|---|
| `crates/webview-core/src/core.rs`（新） | CEF 生命周期（惰性 init_cef / wv_pump / wv_destroy）+ OSR 浏览器（wv_create_browser/resize/destroy）+ 轻量 RenderHandler（OnPaint→BGRA→RGBA→WvOnPaint）+ LoadHandler（on_load_end/error→WvOnLoadStatus） |
| `modules/webview/webview_ffi.h` | 新增 wv_create_browser / wv_resize_browser / wv_destroy_browser |
| `modules/webview/web_panel.h/cpp` | 弃 gdext CefTexture，改 C ABI 渲染：TextureRect 子节点 + paint 回调 → Image+ImageTexture；sync_size 管理浏览器创建/尺寸 |
| `modules/webview/webview_manager.h/cpp` | 面板注册表（browser_id→WebPanel）+ 静态回调 _on_paint/_on_load_status；init_core 传回调 |
| `modules/webview/register_types.cpp` | 移除 load_cef_extension（gdext 路径删除） |
| `misc/scripts/stage_webview.py` | 改 4A 暂存：webview-helper.exe + CEF 运行时（libcef.dll 等 16 项）→ bin/（exe 旁，DLL 搜索+子进程路径） |
| `modules/webview/SCsub` | 链接 CEF 库：libcef.lib（CEF_PATH 定位）+ libcef_dll_wrapper.lib（cargo build 产物 glob） |

### 三个真 bug（排障记录）

| # | Bug | 症状 | 修复 |
|---|---|---|---|
| 1 | `cef::api_hash` 未先调用 | `CefApp_0_CToCpp invalid version -1` 崩溃（debug.log） | init 前调 `api_hash(CEF_API_VERSION_LAST, 0)`（wrap 宏依赖它初始化全局版本） |
| 2 | `NOTIFICATION_RESIZED` 早于 `_ready`（register 分配 id 前） | browser id=-1 → paint 回调按 id 查不到面板 → 空白 | sync_size 加 `browser_id < 0` 守卫；READY 注册后主动 sync_size |
| 3 | `external_begin_frame_enabled` 缺帧驱动 | 页面 load 200 但无 paint | wv_pump 在 do_message_loop_work 后逐浏览器 `send_external_begin_frame()`（godot-cef backend.rs:243 同款） |

**教训**：调试早期问题（崩溃/空白）时，先确认没有旧进程占用（exe 锁 + CEF cache/profile 互斥），再谈逻辑 bug——两次"还是空"都叠加了旧实例干扰。

### 验证状态

**✅ M1b 达成（2026-08-02）**：

- CEF 151.3.12 惰性初始化成功、子进程（webview-helper）正常
- 浏览器创建 + file:// 加载（`load end (status 200)`）
- 软件渲染 paint → WvOnPaint → ImageTexture → **编辑器 dock 显示 bridge.html**
- 输入不可点击 = 预期（M2 输入/IME 适配）

### 遗留 / 下一步

- [x] M1b 渲染链路完成
- [ ] **间歇性启动卡死（已定位嫌疑，待多轮确认）**：review 修复后出现"正在加载停靠面板 20%"卡死。**证据指向 LifeSpanHandler**——两次正常运行的共同点是它已移除（含 on_load_status 已恢复的当前构建），两次卡死构建都有它。**疑似机制**：`WvLifeSpanHandler::on_before_close` 锁 CORE，若在 `wv_create_browser`（持锁建浏览器）或 `wv_pump`（持锁 begin_frame）路径上触发 → 死锁；启动时序差异导致间歇。当前：LifeSpanHandler 保持移除，shutdown 有界泵兜底（干净退出已验证）；**恢复前需分析 CEF 何时在持锁路径触发 on_before_close，M2 前单独做**
- [ ] M2：输入转发（鼠标/键盘/IME 经 C ABI）+ 双向 IPC——当前页面可显示不可交互
- [ ] pump 归属决策（面板级 vs 编辑器级）
- [ ] CEF cache 目录多实例互斥（bin/webview/cef-data）——编辑器单实例约定，记录
