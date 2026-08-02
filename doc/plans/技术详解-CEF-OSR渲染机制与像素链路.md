# 技术详解：CEF OSR 渲染机制与像素链路

> **用途**：深入理解本工程（Godot 编辑器内嵌 CEF 151.3.12）的 OSR（Off-Screen Rendering，无窗口渲染）
> 机制——网页像素如何从 Chromium 内部一路走到 Godot 面板显示。配套代码：`modules/webview/`
> （webview_core.cpp 纯 C++ 核心层 + webview_manager.cpp 桥 + web_panel.cpp 显示层）。
> 行号以 2026-08-02 提交 d51af20db9 为准。

---

## 1. 一句话总览

**OSR = CEF 无窗口渲染**：网页在 CEF 自己的进程里离屏合成，把每一帧像素"拍照"回传给宿主
（Godot），Godot 当普通纹理贴到面板上显示。核心链路：

```
renderer 合成 → 读回 CPU → OnPaint 回调 → 跨进程传像素 → Godot ImageTexture → TextureRect 显示
```

关键认知：**CEF 从不直接画在 Godot 的显示表面上**——渲染发生在 CEF 进程内部，两个进程之间只传
像素快照。Godot 面板是"相框/显示器"，不是 CEF 的画布。

## 2. 进程架构（谁在画什么）

| 进程 | 角色 | 本工程对应 |
|---|---|---|
| 浏览器进程 | 初始化、创建浏览器、接收 OnPaint 帧 | 宿主编译器（Godot 主进程内，CEF 以库方式嵌入） |
| renderer 进程 | 网页 DOM/JS/布局/光栅 | `CefViewWing (Renderer).app` |
| GPU 进程 | 合成加速（mac Metal / Win D3D） | base `CefViewWing.app` 的 `--type=gpu-process` |
| network 进程 | 网络请求 | base `CefViewWing.app` 的 `--type=utility --utility-sub-type=network` |
| 存储进程 | localStorage 等 | base `CefViewWing.app` 的 `--type=utility --utility-sub-type=storage` |

浏览器进程与各子进程通过 mach rendezvous（mac）/ 命名管道（Win）+ mojo IPC 通信；
helper 子进程路径由 `settings.browser_subprocess_path` 指定（绝对路径，CEF 151 硬性要求）。

## 3. 关键开关及其含义（webview_core.cpp）

| 开关 | 位置 | 含义 |
|---|---|---|
| `settings.windowless_rendering_enabled = 1` | :855 | 无窗口模式：CEF 不创建原生窗口，帧经回调交付 |
| `settings.external_message_pump = 1` | :856 | 消息泵由宿主主循环驱动（`CefDoMessageLoopWork`），不占独立线程 |
| `window_info.windowless_rendering_enabled = 1` | :993 | 浏览器实例级 OSR（与全局一致） |
| `window_info.shared_texture_enabled = 0` | :994 | **CPU 读回**：帧以 BGRA 像素经 OnPaint 交付（非 GPU 纹理直通） |
| `window_info.external_begin_frame_enabled = 1` | :995 | 显式产帧：宿主每帧调 `SendExternalBeginFrame` 驱动 renderer 出帧 |

组合效果：**软件交付 + 显式驱动**——帧的产出节奏由 Godot 主循环控制，像素以 CPU 可读形式到达。

## 4. 帧产出的节流与驱动（pump）

```text
Godot 主循环每帧 → WebViewManager → core.pump()
  ├─ 节流判断：前 60 帧(kPumpWarmupFrames)无条件泵送；之后仅当 CEF 置位
  │   pump_requested（OnScheduleMessageLoopWork 回调，任意 CEF 线程原子置位）才泵
  ├─ CefDoMessageLoopWork()          (:899) —— 处理 CEF 浏览器进程消息
  └─ 对每个存活浏览器 SendExternalBeginFrame() (:910) —— 显式请求 renderer 产一帧
```

时序：Godot 帧 → 泵 CEF → 请求产帧 → renderer 合成 → 读回 → OnPaint → 纹理更新 → 本帧显示。
节流的意义：CEF 无事时不多泵（省 CPU）；`SendExternalBeginFrame` 保证 OSR 下 renderer 不会因
"无窗口无事件"而停止产帧（4A 实测坑）。

## 5. 像素链路逐步拆解（一帧的生命周期）

| 步骤 | 发生地 | 代码 | 成本/关注点 |
|---|---|---|---|
| 1. 布局/光栅/合成 | renderer + GPU 进程 | Chromium 内部 | GPU 加速（Metal/D3D）时这里快；纯软件（SwiftShader）时是主要慢源 |
| 2. 合成帧读回 CPU | GPU 进程/浏览器 | Chromium 内部（GpuMemoryBuffer→CPU 映射） | **每帧固定开销**：宽×高×4 字节拷贝；`shared_texture=0` 的代价 |
| 3. OnPaint 回调 | 浏览器进程 | webview_core.cpp:665 | CEF 交付 BGRA 像素 + dirty 矩形 |
| 4. BGRA→RGBA 交换 | 浏览器进程 | webview_core.cpp:685 `handle_paint` | CEF 输出 BGRA（上左原点），Godot 纹理要 RGBA；逐像素交换 R/B |
| 5. 宿主回调 | 浏览器进程 | `cbs.on_paint` → webview_manager.cpp:188 `_on_paint` | 回调期间缓冲有效，必须同步拷贝 |
| 6. 上传纹理 | Godot 主进程 | web_panel.cpp:100 `set_paint` | 尺寸变化：`Image::create_from_data` + `ImageTexture::create_from_image`(:117,:121)；尺寸不变：复用 Image/Texture，`set_data`/`update`(:126) 覆盖，避免每帧重建 |
| 7. 显示 | Godot 主进程 | TextureRect（web_panel.cpp:58 NOTIFICATION_READY 建立） | 面板显示的是上一帧快照 |

两个值得注意的实现点：
- **尺寸变化才重建** Image/ImageTexture（web_panel.cpp:111 判断 `paint_width != p_w`），
  常态帧只做 `memcpy` + `set_data`——把每帧分配压力降到最低。
- **跨进程只传像素**：CEF 侧与 Godot 侧没有共享 GPU 资源（`shared_texture_enabled=0`），
  像素经内存传递后由 Godot 自己上传成纹理。

## 6. 为什么先黑后显示（首帧时序）

```text
面板创建（TextureRect 无纹理 → 显示面板默认背景=黑）
   → renderer 子进程启动（~百 ms 级）
   → 页面加载（file:// 本地，快）
   → 首帧合成+读回 → OnPaint → 纹理更新 → 页面显示（深蓝等页面背景色）
```

- **黑 = 空纹理槽**，不是错误、也不是 CEF 画布。
- **首帧延迟 = renderer 启动 + 页面加载 + 首帧合成**。GPU 加速后亚秒级；
  软件渲染时数秒~数十秒（CEF macosarm64 包的 V8 为 USE_SIMULATOR 构建，JS 执行慢是主因之一）。
- "闪一下" = 首帧到达时纹理从无到有的替换。消除闪烁可加占位背景色/加载态（体验打磨，非必须）。

## 7. 性能模型

| 环节 | 是否 GPU 加速 | 说明 |
|---|---|---|
| 光栅/合成 | ✅（Metal/D3D） | GPU 进程内；`--disable-gpu` 时退回软件（SwiftShader），慢 |
| 帧读回 | ❌ CPU | `shared_texture=0` 的固定成本：宽×高×4 字节/帧；分辨率越高越贵 |
| BGRA→RGBA | ❌ CPU | 同尺寸线性交换，开销与读回同量级 |
| 纹理上传 | ❌ CPU→GPU | Godot 侧上传，与 CEF 无关 |

**结论**：GPU 加速提升的是"画得快"，读回是"送得贵"。静态 UI 读回占比高、收益有限；
滚动/动画时 GPU 光栅优势明显。彻底免读回的路径是 `shared_texture_enabled=1`
（GPU 纹理直通：mac IOSurface/Metal、Win D3D11 共享纹理）——宿主需跨 API 消费 GPU 纹理
（Metal/D3D ↔ Godot renderer 互操作），复杂度高，作为未来优化选项保留（见验证计划-CEF-GPU加速-Win先行.md）。

## 8. 与 Godot 自身渲染的关系

- **两套独立 GPU 栈**：CEF 的 GPU 进程用自己的 GPU context；Godot 用自己的 renderer
  （mac Metal / Win Vulkan·D3D12·GL）。进程隔离，无共享资源、无联动配置。
- **唯一的交叉点**是 Godot 每帧把读回的像素上传成自己的纹理——这一步在 Godot 侧，
  与 CEF 是否 GPU 加速无关，只跟分辨率相关。

## 9. 观测与排查

| 现象 | 观测点 |
|---|---|
| 帧是否产出 | 日志 `[WebView] page loaded ... (status 200)`；临时在 handle_paint 加计数日志 |
| GPU 是否启用 | 进程列表有 `--type=gpu-process`；无 `GPU process exited unexpectedly` |
| 首帧不出现 | 查 renderer/GPU 进程是否存活、是否有崩溃报告（~/Library/Logs/DiagnosticReports/） |
| 桥是否工作 | 页面上实测 `window.cefViewQuery`（见 modules/webview/ui/bridge.html 验证桩） |
| 进程残留 | 退出后 `pgrep CefViewWing` 应为空（异常退出会留 helper，伪装"空白/卡死"） |
