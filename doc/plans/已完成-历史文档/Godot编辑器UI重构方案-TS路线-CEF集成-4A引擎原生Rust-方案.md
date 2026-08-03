# Godot 编辑器 UI 重构（TS 路线）——4A 方案：CEF 引擎原生 Rust 集成

> **定位**：本文件是 CEF 集成形态的最终方案（2026-08-02 用户裁决 **4A**）。取代此前"B-Host（引擎模块托管 godot-cef GDExtension）"路线；与《引擎级WebDock-RouteB-方案.md》的关系：4A 是其 §7 演进路径（B-RustA）的具体化与定案——**复用 godot-cef 的 CEF 通用层，重写 Godot 连接层为引擎原生（C ABI），剥离 gdext**。
>
> **证据标注**：分层规模与 godot 耦合度为源码实测（2026-08-02）；推断标 `[INFERENCE]`。

---

## 1. 决策与理由

**裁决**：不修改 refers/godot-cef 源码；新建 Rust 工程（vendor 其 CEF 通用层），重写 Godot 连接层（C ABI），Rust 核心以 staticlib 融入引擎模块。

| 维度 | B-Host（被取代） | 4A（定案） |
|---|---|---|
| Godot↔CEF 桥 | gdext（GDExtension 接口） | **自有 C ABI**（类型化、无版本缝） |
| 类型缝 | set/call 字符串分发 | **边界按构造类型化**（编译期检查） |
| 引擎升级 | 等 gdext api-4-8 跟进 + 重验 | 只重编自己模块 |
| 上游维护 | gdcef 层白捡上游修复 | 剥离层自维护（接受成本） |
| Rust | 保留（在 gdext 形态里） | 保留（引擎原生） |
| tool/占位语义 | workaround（instantiate_no_placeholders） | **不存在**（模块内原生类） |
| 页面 scheme | file://（受限） | 引擎原生自定义 scheme（editorui://） |

**四个接缝的处置**：tool 语义 ✅ 结构性消除；scheme ✅ 引擎原生；gdext 版本缝 ✅ 消除；类型缝 ✅ 按构造消除（C ABI）。

## 2. 架构总览

```
baize-godot 引擎
├── crates/                      ← 新增 Rust workspace（仓库根，单一 Cargo.lock）
│   ├── Cargo.toml               ← workspace 根
│   └── webview-core/            ← 引擎 webview Rust 核心（CEF）
│       ├── src/ffi.rs           ← C ABI 导出（webview_ffi 契约）
│       ├── src/core.rs          ← 浏览器生命周期/消息泵编排
│       ├── src/cef_app/         ← vendor：cef_app（1689 LOC，0 godot 耦合，MIT）
│       ├── src/software_render/ ← vendor：software_render（206 LOC，MIT）
│       └── src/osr/             ← 抽取：accelerated_osr 逻辑（剥离 gdext 绑定）
├── modules/webview/（C++ 壳——现有 WebPanel/WebDock/register 保留，改造驱动层）
│   ├── web_panel.cpp/h            ← 改：经 C ABI 驱动 Rust core（替代 Object API 驱动 CefTexture）
│   ├── editor_web_dock.cpp/h      ← 不变
│   ├── webview_manager.cpp/h      ← 改：初始化/销毁 Rust core、每帧 wv_pump
│   └── webview_ffi.h              ← C ABI 契约头（C++ 壳与 Rust 共同遵守）
└── 分发目录: bin/webview/         ← CEF 运行时 + ui/（沿用现有 stage 模型）
```

**三层分工**：

| 层 | 职责 | 语言 | 来源 |
|---|---|---|---|
| C++ 壳 | 编辑器集成、纹理上传、输入/IME 适配、EditorDock/WebPanel | C++ | 现有（改造） |
| C ABI | 双方契约（类型化，浏览器语义） | C 头 | 新写 |
| Rust 核心 | CEF 初始化/消息泵/浏览器/OSR/IPC/IME 逻辑 | Rust | vendor + 抽取 + 新写 |

## 3. 复用 / 重写 / 弃用边界（源码实测数据）

| 部分 | 处置 | 依据 |
|---|---|---|
| `cef` / `cef-dll-sys`（CEF 绑定，148.4.0+148.0.10） | **crates.io 依赖**（原样） | 独立发布的库 |
| `cef_app`（1689 LOC：browser/render process、v8 handlers、ipc contract） | **vendor 原样**（MIT，记来源 commit） | 0/9 文件 import godot（实测） |
| `software_render`（206 LOC） | **vendor 原样** | 0/1 文件 import godot（实测） |
| `gdcef` 内部：accelerated_osr/、ime.rs、browser.rs 逻辑 | **抽取改写**（剥离 gdext 绑定，重写 Godot 侧接口为 C ABI） | 35/47 文件 import godot（实测） |
| `gdcef` 的 CefTexture/CefTexture2D 节点类 | **弃用**（替换为 C++ WebPanel） | 编辑器不需要 3D 纹理类 |
| CBOR IPC / cookie / permission 操作 | **弃用或后置**（MVP 用 String 通道；需要时再评估） | — |
| gdext / godot-rust 依赖 | **移除** | 4A 核心目的 |

## 4. C ABI 契约（边界纪律，最高优先）

**纪律**：边界只承载**浏览器语义**（lifecycle/paint/message/input/ime），**禁止** Godot 对象模型（Variant/Object/ClassDB）穿越——否则退化为 mini-gdext。

```c
// webview_ffi.h（草案，实施时定稿）
// 生命周期
WebViewCore *wv_create(const char *exe_dir, const WvCallbacks *cb, void *userdata);
void wv_destroy(WebViewCore *core);
void wv_pump(WebViewCore *core);                      // 消息泵，C++ 壳每帧调用
// 浏览器
void wv_create_browser(WebViewCore *core, int id, const char *url, uint32_t w, uint32_t h);
void wv_destroy_browser(WebViewCore *core, int id);
void wv_resize_browser(WebViewCore *core, int id, uint32_t w, uint32_t h);
void wv_send_message(WebViewCore *core, int id, const char *json);
// 输入 / IME
void wv_input_mouse(WebViewCore *core, int id, const WvMouseEvent *ev);
void wv_input_key(WebViewCore *core, int id, const WvKeyEvent *ev);
void wv_input_ime(WebViewCore *core, int id, const WvImeEvent *ev);
// 回调（C++ 侧实现）
typedef void (*WvOnPaint)(void *ud, int id, const uint8_t *rgba, uint32_t w, uint32_t h);
typedef void (*WvOnMessage)(void *ud, int id, const char *json);
typedef void (*WvOnLoadStatus)(void *ud, int id, int status, const char *url);
```

**类型安全**：两侧签名编译器共同校验，无字符串分发。

## 5. 构建集成与工具链

- **SCons + cargo**：`modules/webview/SCsub` 增加自定义 builder 调 `cargo build --release --target x86_64-pc-windows-msvc`（staticlib），链接进模块；cargo 增量编译快，开发体验与 `task dev` 一体化；
- **工具链决策（免 nightly 的关键杠杆）**：retour-rs（nightly 依赖）只为 **Vulkan hooking** 加速路径服务。**4A 下 fork 编辑器默认渲染器改为 D3D12（Windows）+ Metal（mac）** → 加速路径无需 hooking → **Rust 用 stable**（不再锁 nightly/mise）；Linux Vulkan 后置（届时再评估 hooking 或延迟）；
- Rust 版本与 crate 锁定：CEF 148.0.10 / cef 148.4.0 / 其他依赖 cargo.lock 固定。

## 6. 渲染路径

- **V1 软件渲染**：`WvOnPaint` RGBA 缓冲 → C++ 壳 `ImageTexture` → WebPanel（与 B-Host 同构，已验证可行）；
- **V2 GPU 加速**：D3D12 共享纹理（Windows，fork 4.8 ✓ 支持 get_driver_resource）/ Metal IOSurface（mac）；C++ 壳创建共享资源，经 C ABI 传句柄给 Rust 导入 CEF——**引擎原生集成比 GDExtension 形态更直接**；
- fork 默认渲染器调整（编辑器构建 d3d12）作为前置决策，验证后落地。

## 7. 分发

沿用现有模型：CEF 运行时 + ui/ 产物经 `stage_webview.py` 暂存到 `bin/webview/`，编辑器 exe 相对加载；游戏导出不含（模块 editor-only 门控不变）。仅页面加载从 file:// 升级为**引擎原生 editorui:// scheme**（可选，V1 可先用 file://）。

## 8. 与现有实现的关系

| 现有件 | 处置 |
|---|---|
| WebPanel / WebDockPlugin / register_types（C++） | **保留**，驱动层从 Object API 改 C ABI |
| WebViewManager（扩展加载） | **改**：不再 load_extension，改为创建/销毁 Rust core + 每帧 pump |
| stage_webview.py / 分发模型 | **保留** |
| bin/webview/ 里的 gdext 产物 | **移除**（换 CEF 运行时直接分发） |

## 9. 实施路径（里程碑 + 验收）

```
M0 脚手架（半天-1天）: rust/ crate + SCons cargo 集成 + webview_ffi.h 契约
   验收: task dev 编译通过; C++ 壳创建/销毁 core、wv_pump 空转无崩溃
M1 渲染（1-2天）: CEF 初始化 + wv_create_browser + WvOnPaint → ImageTexture
   验收: WebDock 渲染页面（替代 gdext 托管路径）
M2 桥（1-2天）: wv_send_message / WvOnMessage 双向 + 输入/IME 适配
   验收: MVP2 双向桥全过（选中/改值/拖动/撤销）
M3 分发收尾: editorui:// scheme（或暂留 file://）+ 移除 gdext 产物
   验收: 任意项目打开 WebDock 可用, bin/webview/ 无 gdext 文件
V2（后置）: GPU 加速 D3D12/Metal + fork 默认渲染器调整
```

## 10. 风险清单

| 风险 | 说明 | 对策 |
|---|---|---|
| 剥离层自维护 | gdcef 逻辑（OSR/IME 等）从上游修复变自己修 | vendor 记录来源 commit；上游更新时对照移植；逻辑成熟后稳定 |
| C ABI 纪律 | 边界长大 → mini-gdext | 纪律条款（§4）写入 CR 检查项；API 面增长需评审 |
| FFI 安全 | panic 跨 FFI = UB | 每个导出函数 catch_unwind；无跨边界持有引擎对象 |
| 消息泵线程模型 | CEF 泵在主线程 vs 独立线程 | 沿用 godot-cef 模式（主线程每帧 pump），实测 |
| fork 默认渲染器改动 | 编辑器默认 d3d12 影响面 | M0 单独验证渲染器切换无副作用 |
| 构建链 | cargo 进引擎构建 | MSVC 工具链已验证（godot-cef 同款）；锁版本 |

## 11. 对既有文档的口径修订

| 文档 | 修订 |
|---|---|
| 《RouteB-方案.md》§7 演进路径 | **标记为已被 4A 取代**（B-RustA 具体化定案） |
| 《RouteB-方案.md》§9-10（风险/口径） | gdext 托管相关描述标注"已被 4A 取代" |
| 计划文档 §9（webview 保持 GDExtension 形态） | **修订**：webview = 引擎原生 Rust（4A），GDExtension 形态废弃 |
| 《分发边界说明》 | 主体不变（编辑器带/游戏不带），加载机制描述更新 |
| 实施记录 | 追加 4A 决策与迁移记录 |

## 12. 待核实项

- fork 编辑器默认渲染器切 d3d12 的实际影响（M0）
- cef_app vendor 进 crate 的依赖树（Cargo.toml workspace 调整）
- 软件渲染路径下 on_paint 频率与编辑器帧率匹配（M1）
- mac Metal 路径的 IOSurface 与 fork 原生 Metal 后端集成（V2）
- editorui:// scheme 的 CEF 注册时序（M3）
