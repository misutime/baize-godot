# Godot 编辑器 UI 重构（TS 路线）——CEF C++ 生态复核与从零选型

> **状态**：技术复核结论，待路线裁决（2026-08-02）。
>
> **问题**：既然 `godot-cef` 最值得复用的是与 Godot 解耦的 CEF 应用层，C++ 生态是否已有同类成熟实现，从而避免为了复用 Rust 代码而引入 Rust/C ABI？
>
> **评估边界**：本文件刻意忽略当前 4A 的既有投入（Rust/Cargo/SCons/CEF 151 构建链、M1a/M1b 代码及迁移成本），只比较“从零开始”时的技术选择。Godot 接入层（WebPanel、输入、IME、纹理上传、编辑器桥）在两条路线中都必须自行实现，不作为语言选择的偏置项。
>
> **与既有文档的关系**：本文件补充《Godot编辑器UI重构方案-TS路线-CEF集成-4A引擎原生Rust-方案.md》未覆盖的 C++ 候选集，不自动废止 4A；是否切换路线需在本文的 CEF 151 冒烟门完成后裁决。

---

## 1. 结论

**C++ 生态存在与 `godot-cef/cef_app` 高度对等的通用 CEF 应用层：[`CefViewCore`](https://github.com/CefView/CefViewCore)。** 它是 MIT 许可的纯 C++ 静态库，带独立 CEF 子进程可执行体 `CefViewWing`，由 `QCefView` 与 `CocoaCefView` 共用；核心 API 不泄漏 Qt、OBS、Godot 等宿主对象。

CEF 官方还提供可直接复用的 `CefMessageRouterBrowserSide` / `CefMessageRouterRendererSide`，负责 JS 回调、Render↔Browser IPC、query 生命周期和 context 清理。因此，`godot-cef` 的自有 V8/IPC handler 并非 C++ 路线必须从零重写的能力。

**从零选型建议**：

1. 首选 **C++ + CefViewCore + 官方 CefMessageRouter**；
2. 次选 **C++ + 从官方 cefclient/tests/shared 抽取的最小应用层**；
3. 再考虑 **Rust + godot-cef 的 `cef_app`**；
4. 不建议从 CEF 裸 API 完全自研所有 App/Client/IPC/scheme 语义。

该建议的硬门槛是：**CefViewCore 必须先在 CEF 151 + C++20 下通过构建、子进程、外部消息泵、windowless OSR 和 JS 双向桥冒烟。** CefViewCore 当前默认 CEF 142，尚无 CEF 151 官方验证，不能只凭源码结构直接定案。

---

## 2. 公平比较口径

两条路线共同需要自行实现：

- Godot 模块注册与编辑器生命周期；
- `WebPanel` / `WebDock`；
- Godot 主循环与 CEF 消息泵对接；
- Godot `InputEvent` → CEF 鼠标、键盘、焦点事件；
- Godot IME composition → `ImeSetComposition` / `ImeCommitText`；
- 软件 OSR 缓冲 → Godot `ImageTexture`；
- GPU OSR 共享纹理 → Godot `RenderingDevice`；
- Godot↔页面的产品级编辑语义与 UndoRedo。

因此，下列内容不能作为选择 Rust 的独占理由：

- 输入转换：主要位于 `godot-cef/gdcef/input`，不是 `cef_app`；
- IME 宿主接入：主要位于 `gdcef/cef_texture/ime.rs`；
- GPU 导入：主要位于 `gdcef/accelerated_osr`；
- Godot 纹理和节点：位于 `gdcef/cef_texture*`；
- `software_render` 只提供 popup 合成与测试，不是完整的宿主表面实现。

本次真正比较的是：**在宿主适配层之下，哪条路线能更低成本地提供 CEF App/Client、子进程、消息泵钩子、V8/IPC、scheme 和浏览器事件管线。**

---

## 3. C++ 候选盘点

| 候选 | 形态 | 许可 | 通用性 | 结论 |
|---|---|---|---|---|
| **CefViewCore** | C++ 静态库 + helper | MIT | 核心仅依赖 CEF/STL；宿主通过 delegate 接入 | **首选候选** |
| CEF `cefclient` / `tests/shared` | 官方参考应用/源码 | BSD-3-Clause | 功能最全，但不是发布的稳定库 | 抽取最小层的次选 |
| CEF `cefsimple` / `cef-project` | 官方最小示例/模板 | BSD-3-Clause | 生命周期示例完整，OSR/IME/高级 IPC 不完整 | 仅作为脚手架 |
| QCefView | Qt QWidget 封装 | LGPL-3.0 | OSR/IME/GPU 完整，但强耦合 Qt | 使用其 CefViewCore；Qt 层只作参考 |
| `webview_cef` | Flutter C++ 插件 | Apache-2.0 | CEF 149、GPU/IME 完整，native core 较干净，但含 Flutter FFI | CEF 149/151 GPU、IME 参考 |
| `obs-browser` | OBS CEF 插件 | GPL-2.0 | 生产级 OSR/GPU，但强耦合 libobs | GPU 路径参考；不可作为依赖 |
| CEF Views | 官方窗口化 UI API | BSD-3-Clause | Chrome/Views 窗口化；不满足 windowless OSR | 排除 |

### 3.1 官方资产的边界

CEF 官方提供：

- `libcef`；
- 生成的 C++ API 包装层 `libcef_dll_wrapper`；
- `CefMessageRouter`；
- `cefclient` 完整参考实现；
- `cefsimple` 与 `cef-project` 模板。

官方没有把 `cefclient` 的 App/Client/OSR/消息泵代码发布成稳定静态库。官方定位也是“reference implementation / feature demonstration”，复用时需要抽取和去除 WTL/GTK/AppKit/测试框架耦合：

- [CEF tests README](https://github.com/chromiumembedded/cef/blob/master/tests/README.md)
- [CEF cefclient](https://github.com/chromiumembedded/cef/tree/master/tests/cefclient)
- [CEF general usage](https://github.com/chromiumembedded/cef/blob/master/docs/general_usage.md)
- [cef-project](https://github.com/chromiumembedded/cef-project)

所以“官方没有通用应用层库”成立；但“C++ 生态没有通用应用层库”不成立，因为 CefViewCore 填补了这个缺口。

---

## 4. CefViewCore 深入核查

### 4.1 项目形态与维护状态

CefViewCore README 明确声明：

- 是 CefView 系列的核心抽象层；
- `QCefView` 和 `CocoaCefView` 均基于它；
- 主要由 C++ 编写，macOS helper 含少量 Objective-C；
- 目标是作为不同高层绑定的公共基础层。

构建目标：

```cmake
add_library(CefViewCore STATIC ...)
add_executable(CefViewWing ...)
```

证据：

- [CefViewCore README](https://github.com/CefView/CefViewCore)
- [CefViewCore CMake 目标](https://github.com/CefView/CefViewCore/blob/main/src/CMakeLists.txt)
- [MIT License](https://github.com/CefView/CefViewCore/blob/main/LICENSE)
- [仓库元数据](https://api.github.com/repos/CefView/CefViewCore)
- [最近提交](https://api.github.com/repos/CefView/CefViewCore/commits?per_page=5)

截至 2026-08-02，仓库最近 push 为 2026-05-18；CI 覆盖 Windows/macOS/Linux x64，并含 Linux arm64。它仍在维护，但团队规模较小，仍需锁 commit 和自建升级回归。

### 4.2 能力矩阵

| 能力 | CefViewCore 覆盖 | 仍需宿主实现 |
|---|---|---|
| CEF Browser/Render App | `CefViewBrowserApp` | `CefInitialize` / `CefShutdown` 调用 |
| CEF 子进程 | `CefViewWing` | 打包路径与启动参数 |
| 外部消息泵 | `OnScheduleMessagePumpWork` delegate | 定时调度 `CefDoMessageLoopWork` |
| Browser Client handlers | 生命周期、加载、权限、下载、键盘、焦点、OSR 等统一转发 | 选择需要的回调及产品行为 |
| V8/IPC | `CefViewBridgeObject` + `CefMessageRouter` | 页面协议映射 |
| 自定义 scheme | 内置 scheme、目录和归档资源 provider | `editorui://` 的具体资源域与权限 |
| 软件 OSR | `OnPaint` 转发 | popup 合成、脏区策略、纹理上传 |
| GPU OSR | `OnAcceleratedPaint` 转发 | D3D11/D3D12、IOSurface/Metal 导入 |
| IME 通知 | composition range、text selection、virtual keyboard 回调 | Godot IME 输入映射与候选窗定位 |
| 输入 | keyboard/focus handler 回调 | Godot 输入事件发送到 `CefBrowserHost` |

关键源码：

- [`CefViewBrowserApp`](https://github.com/CefView/CefViewCore/blob/main/include/CefViewBrowserApp.h)：scheme、Browser/Render Process Handler、消息泵调度；
- [`CefViewBrowserAppDelegate`](https://github.com/CefView/CefViewCore/blob/main/include/CefViewBrowserAppDelegate.h)：命令行、子进程、消息泵宿主边界；
- [`CefViewBrowserClientDelegate`](https://github.com/CefView/CefViewCore/blob/main/include/CefViewBrowserClientDelegate.h)：Browser Client 与 OSR/IME 回调；
- [`CefViewRenderApp`](https://github.com/CefView/CefViewCore/blob/main/src/CefWing/App/CefViewRenderApp.cpp)：V8 context、MessageRouter、事件派发；
- [`CefViewBridgeObject`](https://github.com/CefView/CefViewCore/blob/main/src/CefWing/Bridge/CefViewBridgeObject.h)：native method、event listener、JS result；
- [`CefViewCore src/CMakeLists.txt`](https://github.com/CefView/CefViewCore/blob/main/src/CMakeLists.txt)：静态库、helper 和 CEF 运行时分发。

### 4.3 与 `godot-cef/cef_app` 的对照

| 能力轴 | CefViewCore | `godot-cef/cef_app` |
|---|---|---|
| App/Browser/Render Process 组装 | 完整 | 完整 |
| 子进程 helper | `CefViewWing` | `gdcef_helper` |
| 外部消息泵钩子 | 有 | 有 |
| Browser Client handlers | 覆盖面更宽 | 较多逻辑在 `gdcef` |
| V8 对象 | `CefViewBridgeObject` | 自有 `v8_handlers.rs` |
| 通用异步 IPC | 官方 `CefMessageRouter` | 自有文本/二进制/CBOR handler |
| custom scheme | 内置并支持目录/归档资源 | `cef_app` 注册，`gdcef/godot_protocol` 处理 |
| OSR 回调 | 转发 | 转发 |
| popup 软件合成 | 无 | `software_render` 有约 207 LOC |
| 输入/IME/GPU 宿主实现 | 无 | 同样主要位于 Godot 耦合的 `gdcef` |

结论：**CefViewCore 可替代 `cef_app` 的主要价值；`software_render` 的 popup 合成仍需保留思路或用少量 C++ 重写。** 这不是路线障碍，因为渲染表面本来就属于宿主边界。

---

## 5. CEF 官方 MessageRouter 的复用价值

CEF C++ wrapper 已提供通用异步 JS↔Native 路由：

- Renderer 侧负责注入 `window.cefQuery`、管理 V8 context 和 JS callbacks；
- Browser 侧通过 Handler 接收 query；
- 自动管理 query ID、success/failure、persistent query、frame/browser 销毁清理；
- Browser/Renderer 两侧通过 CEF process message 通信。

参考：

- [CEF JavaScript integration](https://github.com/chromiumembedded/cef/blob/master/docs/javascript_integration.md)
- [CEF general usage：Generic Message Router](https://github.com/chromiumembedded/cef/blob/master/docs/general_usage.md)
- [cefclient renderer 的 MessageRouter 接入](https://github.com/chromiumembedded/cef/blob/master/tests/cefclient/renderer/client_renderer.cc)

若页面桥只需要类型化 JSON/String 请求与异步响应，应优先复用 MessageRouter，而不是重新实现一套 V8 handler + Browser/Renderer IPC。只有下列需求才值得自建协议：

- 二进制零拷贝；
- 流式/高频消息；
- 特殊 backpressure；
- 跨 frame 的自定义路由；
- MessageRouter 无法表达的生命周期语义。

当前编辑器桥的选中、改值、拖动、撤销等低频命令不构成这些条件。

---

## 6. OSR、输入与 IME：不存在完全脱离宿主的通用实现

这部分 C++ 和 Rust 结论相同：CEF 只能定义接口，最终实现必须了解宿主窗口、输入系统和渲染设备。

### 6.1 软件 OSR

CEF `OnPaint` 提供 BGRA 缓冲和 dirty rect；宿主自行决定如何绘制。官方文档明确指出 cefclient 使用 OpenGL，但应用可使用任意方式：

- [CEF Off-Screen Rendering](https://github.com/chromiumembedded/cef/blob/master/docs/general_usage.md#off-screen-rendering)
- [`CefRenderHandler`](https://github.com/chromiumembedded/cef/blob/master/include/cef_render_handler.h)

`godot-cef/software_render` 值得复用的核心主要是 popup 合成、裁剪与测试。这段逻辑可重新实现为很小的 C++ utility，不足以单独决定语言路线。

### 6.2 GPU OSR

CEF 通过 `OnAcceleratedPaint` 把共享纹理句柄交给应用。句柄可能每帧变化，不能在回调外缓存；应用需每次打开并复制到自有纹理：

- [`CefRenderHandler::OnAcceleratedPaint`](https://github.com/chromiumembedded/cef/blob/master/include/cef_render_handler.h#L156-L179)
- [cefclient Windows D3D11 OSR](https://github.com/chromiumembedded/cef/blob/master/tests/cefclient/browser/osr_render_handler_win_d3d11.cc)
- [obs-browser GPU 导入参考](https://github.com/obsproject/obs-browser/blob/master/browser-client.cpp)

CefViewCore 只负责把回调转给宿主，这是正确的分层。Godot 的 D3D12/Metal 导入无论 C++ 或 Rust都必须定制。

### 6.3 输入与 IME

CEF OSR 不拥有原生窗口，因此宿主必须：

- 发送 mouse/key/focus 事件；
- 调用 `ImeSetComposition` / `ImeCommitText` / `ImeCancelComposition`；
- 根据 `OnImeCompositionRangeChanged` 定位候选窗。

可参考：

- [QCefView 完整 IME 管道](https://github.com/CefView/QCefView/blob/main/src/details/QCefViewPrivate.cpp#L976-L1022)
- [CEF cefclient Windows IME](https://github.com/chromiumembedded/cef/blob/master/tests/cefclient/browser/osr_ime_handler_win.cc)
- [`webview_cef`](https://github.com/hlwhl/webview_cef) 的 CEF 149/C++20 IME 管道。

这部分是 Godot 接入层职责，不能计入 `cef_app` 或 CefViewCore 的缺失。

---

## 7. 从零比较：C++ 与 Rust

| 维度 | C++ + CefViewCore | Rust + godot-cef 通用层 |
|---|---|---|
| CEF App/Client 复用 | CefViewCore 直接提供 | vendor `cef_app` |
| V8/IPC | 官方 MessageRouter + CefViewBridge | 自有 Rust handlers |
| 子进程 | CefViewWing | helper Rust binary |
| Godot 边界 | C++ 直接调用 | 自有 C ABI + callbacks |
| 构建链 | SCons/CMake + CEF wrapper | SCons/Cargo/CMake/Ninja + CEF wrapper |
| 类型与生命周期 | 单一 C++/CEF 对象模型 | Rust/C++ 间手工 FFI 所有权约定 |
| GPU 共享纹理 | C++ 中直接持有 CEF/Godot 平台对象 | 句柄和生命周期需跨 C ABI |
| 内存安全 | 依赖 C++ 纪律、CEF RefPtr | Rust 核心有更强内存安全 |
| CEF 版本跟进 | CefViewCore 当前默认 142，151 需验证 | cef-rs 已有 151，vendor 代码已能编译的证据来自既有路线，但本比较不计沉没投入 |
| 第三方维护风险 | CefViewCore 小团队 | godot-cef 小团队 + cef-rs |

### 7.1 C++ 路线的主要优势

- 不需要自有 Rust↔C++ C ABI；
- 不需要跨语言传递回调、字符串、buffer 和共享纹理句柄；
- CEF `CefRefPtr`、Handler、BrowserHost 与 Godot C++ 壳处在同一对象模型；
- 可直接使用官方 MessageRouter；
- 调试栈和构建工具更短；
- V2 GPU 导入路径更直接。

### 7.2 Rust 路线仍有的优势

- OSR buffer、IPC、状态机等易错代码具有更强内存安全；
- `godot-cef` 已经验证过一套 CEF App/V8/IME 语义；
- `cef-rs` 对最新 CEF 分支跟进较快；
- CEF C++20 与 Godot C++ 标准可被 Rust staticlib/C ABI 隔离。

在不计既有投入时，这些优势仍有价值，但不足以自动压过 C++ 路线减少跨语言边界和直接复用 CefViewCore/MessageRouter 的收益。

---

## 8. 关键风险与硬门槛

### 8.1 CefViewCore 尚未验证 CEF 151

CefViewCore 当前默认：

```cmake
set(DEFAULT_CEF_SDK_VERSION "142.0.15+...+chromium-142.0.7444.176")
```

源码已有多处 CEF 124/125/126/138 的条件适配，但没有 CEF 151 CI 或发布声明。143→151 的 API 漂移必须实测，不能推断通过。

### 8.2 CEF 当前要求 C++20

CEF 当前 CMake 编译配置包含：

```cmake
-std=c++20
```

证据：[CEF `cef_variables.cmake.in`](https://github.com/chromiumembedded/cef/blob/master/cmake/cef_variables.cmake.in#L126-L149)。

需要验证：

- CefViewCore 以 C++20 编译；
- Godot 模块是否能按文件或独立静态库隔离 C++20；
- `/MD`/`/MT`、异常、RTTI、线程安全静态等编译选项与 Godot 是否一致；
- `libcef_dll_wrapper` 与主程序链接无 CRT 冲突。

### 8.3 CefViewCore API 面偏宽

`CefViewBrowserClientDelegateInterface` 暴露约 40 个虚回调。项目只需其中一部分。建议：

- 不让该宽接口直接穿透 WebPanel；
- 提供项目自有窄接口；
- 不需要的 handler 用默认实现；
- 若直接 vendor，裁掉 archive、download、permission、JS dialog 等当前不需要的能力，但保留上游来源 commit 和许可证。

### 8.5 CefViewCore 修改与裁剪授权（2026-08-02 用户裁决，已升级）

分层修改权限（影响后续所有判断）：

| 层 | 可否修改 | 依据 |
|---|---|---|
| **CefViewCore**（小项目，维护不积极） | **直接 vendor 进仓库（`thirdparty/cefviewcore/`），与上游完全断开，可自由修改** | MIT 许可、~几千行、就是要嵌入本产品的层；不复用外部 `refers/` 路径（消除构建链不可复现）；记录来源 commit 后断开 |
| **CEF 官方源码**（`refers/cef` 源码树） | **不改** | 本项目不构建 CEF 源码（重编 Chromium 成本数小时~数天）；只消费预编译 binary SDK，API 面由 `CEF_API_VERSION` 机制管理 |
| **CEF 预编译 SDK**（`cef-dist/`，如 151.3.12 standard 包） | **不改** | 黑盒二进制依赖：`libcef.dll` + `libcef_dll_wrapper` + 头文件；通过锁版本管理 |

落地形态（2026-08-02 修订）：

- **第一步（当前）**：CefViewCore 源码拷贝进 `thirdparty/cefviewcore/`（记录上游 commit + MIT license；排除构建产物/.git），SCsub 调其自带 CMake 构建（方式二），后续直接改仓库内源码。
- **裁剪时机**：C0 通过、API 面稳定后，对比实际使用能力与全量（archive/download/permission/JS dialog/drag 等不需要的 handler）；或编译时间/体积/宽接口穿透（§8.3）实际造成问题时，裁掉不需要能力（可移入 `modules/webview/` 或就地裁剪）。
- **代价权衡**：已接受——与上游断开，升级 CEF 版本时自维护 diff。

### 8.4 第三方维护风险

CefViewCore 与 godot-cef 都属于小团队维护。无论选哪条路线，都应：

- 锁 commit 和 CEF 版本；
- 记录来源；
- 建立初始化、OSR、IPC、IME、GPU 回归；
- 升级时对照官方 CEF branch/API version；
- 不依赖“上游会及时修复”的假设。

---

## 9. 决策前验证（C++ Gate C0）

目标：用最小可移除实验回答“CefViewCore 能否在当前工具链与 CEF 151 下承担通用应用层”。不接 Godot 产品功能，不迁移现有代码。

### C0.1 构建

- CEF 151 SDK；
- C++20；
- `CefViewCore` staticlib；
- `CefViewWing` helper；
- Windows x86_64 MSVC；
- 验证 CEF runtime staging；
- 验证无 CRT、符号或 CEF API version 冲突。

**验收**：clean build 和增量 build 均通过；helper 能被启动并正确退出子进程。

### C0.0 地基审核结论（2026-08-02 shifu 评审，已实测验证）

> 总判：**可进 C0，不能按现案固化**。以下发现直接决定 SCsub 与核心层契约。

| # | 结论 | 状态 |
|---|---|---|
| 1 | **CRT 冲突（硬雷）**：Godot 默认 `use_static_cpp=True` → `/MT`（`platform/windows/detect.py:399-400`）；CefViewCore 默认 `STATIC_CRT=OFF` → `/MD`。静态库 CRT 不匹配会链接失败/运行崩溃。**修复：`-DSTATIC_CRT=ON` 构建（已实测：vcxproj 确认 `MultiThreaded`）** | ✅ 已修复 |
| 2 | 异常模式：`_HAS_EXCEPTIONS=0` 仅 STL 策略，实测三库仍 `/EHsc` 编译；链接安全，但 **delegate 异常会逃入 CEF，宿主回调必须 catch-all** | ✅ 已补 |
| 3 | `OnScheduleMessagePumpWork` 可来自任意线程：只能原子排期（`std::atomic<bool>`），不能碰非原子宿主状态 | ✅ 已补 |
| 4 | cache 路径不放 exe 目录：改 `%LOCALAPPDATA%/baize-godot/cef`（CEF≥120 同 root 单例；双开需独立 root 或 OnAlreadyRunningAppRelaunch） | ✅ 已补 |
| 5 | `OnBeforeClose` 不能删（需清 router/registry），应消除持锁回调——与 4A 删 LifeSpanHandler 不同，我们保留回调但无锁 | ✅ 已实现 |
| 6 | C++20 仅限 CEF TU（`clone→remove /std:c++17→append /std:c++20`），壳层保持 C++17；`webview_core.h` 必须 C++17 兼容、不暴露 CEF 类型 | ✅ 已确认 |
| 7 | stage 须取同次 151 产物并验 hash；主 exe 直链 libcef 时 CEF DLL 放 exe 旁（或 delay-load+AddDllDirectory）；版本化避锁 | ⏳ stage 切片 |
| 8 | helper 无需额外 manifest（`mt.exe` 已嵌 asInvoker）；用 `mt.exe` 验收，测 DPI/提权父进程 | ⏳ C0.2 |

额外确认：CEF 151.3.12 为当前 **stable**（非 beta，官方 JSON 实测）；CefViewCore 在 151.3.12 + C++20 下构建成功（库 + helper + 完整运行时）。

### C0.0b 构建契约（2026-08-02 用户裁决：混合构建，已实施验证）

**形态**（详见《Godot编辑器UI重构方案-TS路线-CEF集成-C++路线-构建集成方案分析.md》）：

```text
SCsub 编入：CefViewCore 源码（thirdparty/cefviewcore/src/Shared + CefView/CefBrowserApp，24 文件，显式清单）
stage 预构建：libcef_dll_wrapper.lib + CefViewWing.exe + CEF 运行时（首次/换版本才构建，cf. 版本标记）
链接：module_webview（含全部 CefViewCore 符号）→ libcef.lib（SDK）→ libcef_dll_wrapper.lib（stage 产物）
```

关键约定（已实测）：

- **CefViewCore 源码编入 SCons**（会改的层走增量编译）；`CefViewWing.exe`/`libcef_dll_wrapper.lib`/CEF 运行时走 stage 预构建（锁版本后几乎不变）。
- stage 预构建：`subprocess.run` 调 cmake（不经 SCons/mySpawn，避免 Godot 的 spawn/emitter 坑），`-DSTATIC_CRT=ON -DCMAKE_CXX_STANDARD=20`；产物标记 `cef-version.txt`，首次/换版本才构建，否则跳过。
- 模块侧 CEF TU：C++20 + `NOMINMAX` + `WIN32_LEAN_AND_MEAN` + **`NDEBUG`**（CEF 151 头文件实测 C++17 编不过、缺 NOMINMAX 报 min/max 冲突；NDEBUG 必须——Release wrapper 把 `~RefCountedThreadSafeBase` 等内联为 `= default`，Godot dev 构建不定义 NDEBUG 时 `DCHECK_IS_ON()=true` 引用外部析构导致 LNK2019，已实测修复）。
- 壳层 TU：C++17，仅 include `webview_core.h`（零 CEF 依赖）。
- CEF 标准库走 `LINKFLAGS`（`LIBS` 裸名会被 Godot `redirect_build_objects` emitter 误判为构建目标报 LNK1181）。
- 已踩坑记录：SCons 默认 `ENV` 缺 `WINDIR`（cmake CompilerId 死锁，用完整 `os.environ` 解决）；`methods.py:388` `Popen(text=True)` GBK 解码（stage 用 `subprocess.run` 不经 SCons 规避）。

### C0.2 生命周期与消息泵

- `CefInitialize` / `CefShutdown`；
- external message pump；
- `OnScheduleMessagePumpWork`；
- windowless browser 创建/关闭。

**验收**：空白页面稳定运行、关闭无残留进程、消息泵无饥饿/忙轮询。

### C0.3 软件 OSR

- `OnPaint`；
- resize；
- popup；
- dirty rect；
- 连续运行。

**验收**：RGBA/BGRA 校验正确，resize/popup 正常，无持续内存增长。

### C0.4 JS 双向桥

- `CefMessageRouter` 的 `cefQuery`；
- C++→JS event；
- frame/context 销毁；
- 超时或 browser close 时 query 清理。

**验收**：文本请求/响应双向通过，刷新和关闭不留悬空 callback。

### C0.5 Win IME 最小验证

- editable node focus；
- composition range；
- 拼音候选；
- commit/cancel；
- 中英文混输。

**验收**：候选框位置正确，无吞字、重复提交或 composition 残留。

### C0 裁决规则

- **全部通过**：从零路线默认裁决为 C++ + CefViewCore/MessageRouter；
- **仅 CefViewCore 151 兼容失败，官方 API 正常**：评估 vendor 修复或改从 cefclient 抽取，不能直接回到 Rust；
- **C++20/CRT/工具链与引擎无法隔离**：Rust staticlib/C ABI 的工具链隔离价值成立，重新比较 4A；
- **CefViewCore 宽接口/协议不适配且裁剪成本过高**：改用官方 `CefMessageRouter` + 最小自有 App/Client；
- **OSR/IME/GPU 仍需大量宿主代码**：这是两路线共同成本，不单独判 Rust 或 C++ 失败。

---

## 10. 建议架构（C++ 候选）

```text
Godot 编辑器
└── modules/webview/                    C++，Godot 宿主层
    ├── WebPanel / WebDock
    ├── Godot Input / IME adapter
    ├── ImageTexture / RenderingDevice adapter
    └── Editor bridge / UndoRedo
             │
             ▼
    webview-core-cpp                    C++，CEF 通用层
    ├── CefViewCore（锁 commit，必要时裁剪）
    ├── CefMessageRouter
    ├── lifecycle/browser registry
    ├── editorui:// scheme policy
    └── software popup compositor
             │
             ▼
    libcef + libcef_dll_wrapper
    CefViewWing helper
```

边界纪律（两层）：

- `webview-core-cpp` 公开 API 面（webview_core.h）保持纯 C++（std::string/回调，零 Godot 类型，接口契约）；
  内部实现（webview_core.cpp）因 TU 约束不得 include 任何 Godot 头：该编译单元必须 include CEF 头，
  而 CEF 的 net_error 枚举与 Godot 的 enum Error 成员重名（ERR_OUT_OF_MEMORY 等），同 TU 共存
  必然 C2365 冲突（与 include 顺序无关，含 typedefs.h 链的 Godot 头一概不能进入）；日志/计时
  走标准库（stderr / std::chrono），Godot 侧日志由宿主导航层负责；
- Godot 对象不进入 CefViewCore delegate；
- CEF 对象不穿透到 WebPanel 的产品 API；
- 桥协议只承载浏览器/编辑器命令语义；
- GPU 句柄生命周期严格限制在 `OnAcceleratedPaint` 回调契约内；
- 自定义 scheme 与页面资源权限由单一层管理。

---

## 11. 对 4A 方案的影响

现有 4A 方案的核心依据是：

- `cef_app` 与 `software_render` 无 Godot 耦合；
- 复用它们可以避免重做 CEF App/V8/IPC/OSR 语义；
- 重写 Godot 连接层为 C ABI。

该依据本身成立，但候选集不完整：方案没有评估 CefViewCore、官方 MessageRouter，以及从 cefclient/tests/shared 抽取最小 C++ 层。参见原方案：

- `Godot编辑器UI重构方案-TS路线-CEF集成-4A引擎原生Rust-方案.md:53-63`
- `Godot编辑器UI重构方案-TS路线-CEF集成-4A引擎原生Rust-方案.md:65-90`

因此应把原结论修订为：

> Rust 复用是可行方案，但并非唯一能避免重复验证 CEF 语义的方案。C++ 可通过 CefViewCore + 官方 MessageRouter 获得相近甚至更宽的通用应用层，并消除自有 Rust/C++ C ABI。若从零决策，默认倾向 C++；最终裁决取决于 CefViewCore + CEF 151 + C++20 的 C0 验证。

本文件不处理“当前已有 4A 投入是否应废弃”的迁移问题；该问题必须另行加入沉没成本、当前 M1b 验证状态和切换风险后裁决。

---

## 12. 权威与项目来源

### CEF 官方

- [Chromium Embedded Framework](https://github.com/chromiumembedded/cef)
- [General Usage](https://github.com/chromiumembedded/cef/blob/master/docs/general_usage.md)
- [JavaScript Integration](https://github.com/chromiumembedded/cef/blob/master/docs/javascript_integration.md)
- [cefclient](https://github.com/chromiumembedded/cef/tree/master/tests/cefclient)
- [CEF tests 定位](https://github.com/chromiumembedded/cef/blob/master/tests/README.md)
- [`CefRenderHandler`](https://github.com/chromiumembedded/cef/blob/master/include/cef_render_handler.h)
- [`CefMessageRouter` 使用参考](https://github.com/chromiumembedded/cef/blob/master/tests/cefclient/renderer/client_renderer.cc)
- [cef-project](https://github.com/chromiumembedded/cef-project)

### C++ 通用层与宿主实现

- [CefViewCore](https://github.com/CefView/CefViewCore) — MIT，通用 C++ CEF 应用层
- [QCefView](https://github.com/CefView/QCefView) — LGPL-3.0，Qt OSR/IME/GPU 实现参考
- [webview_cef](https://github.com/hlwhl/webview_cef) — Apache-2.0，CEF 149/C++20/Flutter GPU+IME 参考
- [obs-browser](https://github.com/obsproject/obs-browser) — GPL-2.0，生产级 GPU 导入参考

### 本仓库既有方案

- `doc/plans/Godot编辑器UI重构方案-TS路线-CEF集成-4A引擎原生Rust-方案.md`
- `doc/plans/Godot编辑器UI重构方案-TS路线-引擎级WebDock-RouteB-方案.md`
- `doc/plans/实施记录-CEF-4A引擎原生Rust.md`
