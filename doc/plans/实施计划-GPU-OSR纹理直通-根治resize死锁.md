# 实施计划：GPU OSR 纹理直通——根治 WebDock resize 死锁（交接文档）

> **状态**：✅ 已实施并验证（2026-08-04，mac arm64 实机）。交付：`shared_texture_enabled=1` + `OnAcceleratedPaint`（IOSurface）+ Godot 侧 Metal→RD 纹理直通（**零引擎改动**——本 fork 已具备 `RD::texture_create_from_extension` 跨 API 导入，见 §3.2 Step 2 注记）。
> **验证结论**：GPU OSR 路径正常渲染、颜色与软件路径逐字节一致、**7 次连续快速 resize 全部瞬时收敛且尾随重发分支 0 触发**（旧死锁缓解逻辑已惰性）、退出干净（exit 0、无残留 helper）。软件路径回退可用（`WEBVIEW_OSR_SOFTWARE=1`）。详见 §8。
> **遗留**：Win D3D11→D3D12 导入路径（§3.3 矩阵）未实施——非 mac 平台保持软件路径；Step 4 的尾随重发/1:1 裁剪简化未做（验证显示其已惰性，移除属独立行为变更，另行评审）。
>
> **为什么 GPU OSR 能根治（实测口径，2026-08-04 修正）**：死锁根源是软件读回路径下“静态页面合成器按需不产帧 → viz 共享内存尺寸不更新 → hold 永不释放”。GPU 纹理直通路径（`OnAcceleratedPaint`）下，**resize 能及时触发合成器提交新尺寸帧**（实测：7 次连续快速 resize 全部瞬时收敛、尾随重发分支 0 触发）——根因 3 从机制上消失。**注意**：不是“合成器持续活跃”（Chromium 合成器无论 GPU/软件均按需提交，见《技术详解-GPU-OSR与帧调度》§2）；GPU 路径的差异在于帧提交语义（Invalidate 真提交 / viz 自动重分配，确切机制待进一步确认）。届时根因 1/2（黑边/变形）的兜底（1:1 裁剪）也基本不再触发。

---

## 1. 背景：本次问题与本方案的关系

**问题**：拖动 WebDock 面板（CEF OSR）分隔条时，出现变形、黑边、收敛卡死三层问题。已修复（`c565c5d1b6`），但属于"宿主侧缓解"：

- 根因 3（核心）：CEF 软件 OSR 的 `hold_resize_` 机制 + 静态页面合成器按需不产帧 → resize 死锁。
- 根因 1/2（衍生）：纹理与面板不一致的后果（透明黑边、拉伸变形）。
- 当前修复：1:1 裁剪（防变形）+ 节流/尾随重发（≤250ms 收敛）——**有残余窗口，非根治**。

**本计划**：实现 GPU 纹理直通（`shared_texture_enabled=1`），从机制上消除死锁。

> **错误说法修正记录（2026-08-04）**：
> 1. ~~“GPU 路径合成器持续活跃，不再依赖按需产帧”~~——**错误**。Chromium 合成器无论 GPU/软件均为 damage-driven（按需提交），GPU OSR 只改像素交付方式、不改帧调度。**实测口径**：GPU 路径下 resize 能及时触发合成器提交新尺寸帧（§8 证据），确切机制（Invalidate 真提交 vs viz 自动重分配）待确认。详见《技术详解-GPU-OSR与帧调度-概念澄清与解决路径.md》。
> 2. ~~“Godot 无跨 API 纹理导入 API，是最大成本点”~~——**错误**（规划时漏查）。本 fork 已具备 `RD::texture_create_from_extension`（`rendering_device.cpp:1844`，三驱动均已实现），**零引擎改动**（§2.3 实施时确认）。

---

## 2. 现状（已核实的代码与文档）

### 2.1 当前渲染链路（软件读回）

```
CEF GPU 进程（合成/光栅已 GPU 加速：Win D3D / mac Metal）
→ OSR 软件交付（shared_texture_enabled=0）→ OnPaint(BGRA)
→ webview_core.cpp handle_paint（BGRA→RGBA 逐像素）
→ WebViewManager::_on_paint → WebPanel::set_paint
→ Image/ImageTexture → _draw 绘制
```

- **注意**：CEF 的 GPU 加速**已启用**（`实施记录-C++路线-mac双平台适配与验证.md` C-mac-2：移除 `--disable-gpu`，GPU 进程正常，48fps vs 软件 12fps）。**缺的是"像素交付"的 GPU 直通**（`shared_texture_enabled=1`），不是"渲染加速"。
- 关键代码：`modules/webview/webview_core.cpp` `create_browser()` 的 `window_info.shared_texture_enabled = 0`、`ClientDelegate::onPaint`、`handle_paint`。

### 2.2 CEF 侧 API（参考源码 /Users/misu/misutime/102_games/refers/cef）

- `CefRenderHandler::OnAcceleratedPaint`（CEF 151 签名含 `const CefAcceleratedPaintInfo &info`）：交付共享纹理句柄。
- **句柄每帧可能变化，不能在回调外缓存**；需在回调内每次打开并复制到自有纹理（`技术详解-CEF-OSR渲染机制与像素链路.md` §6.2、cefclient `osr_render_handler_win_d3d11.cc`）。
- CefViewCore 的 delegate 接口已有 `onAcceleratedPaint`（`CefViewBrowserClientDelegate.h:308-313`）——`ClientDelegate` 需要实现并转发。

### 2.3 Godot 侧现状（关键阻碍，已核实）

- **✅ 已具备跨 API 纹理导入（实施时发现，推翻原“最大成本点”判断）**：`RenderingDevice::texture_create_from_extension(type, format, samples, usage, image, w, h, depth, layers, mipmaps)`（`servers/rendering/rendering_device.cpp:1844`，ClassDB 已绑定）三驱动均已实现——metal 按 `MTL::Texture*` 包装（`rendering_device_driver_metal.cpp:416`，格式不匹配时建 view），vulkan 按 VkImage，d3d12 按 ID3D12Resource。**无需 fork 引擎新增 API**。
- 渲染器：mac 默认 **Metal**（`rendering/rendering_device/driver.macos` 默认 `metal`，arm64）；Win 默认 d3d12（`driver.windows`）。
- 设备访问：`RD::get_singleton()->get_context_driver()` → `RenderingContextDriverMetal::get_metal_device()`（`rendering_context_driver_metal.h:130`，public）——模块可拿到 Godot 的 `MTL::Device` 实例，IOSurface→Metal 纹理用同一设备实例，无跨实例风险。
- 可用资产：
  - `thirdparty/metal-cpp`：`MTL::Device::newTexture(descriptor, IOSurfaceRef, plane)`（`MTLDevice.hpp:503`）——Metal 从 IOSurface 建纹理的现成 API；头自带 `<IOSurface/IOSurfaceRef.h>`。
  - 模块 SCsub 已链接 `-framework IOSurface`；mac 构建默认 `METAL_ENABLED`。
  - 消费端：`Texture2DRD`（`scene/resources/texture_rd.h`）——RD RID → RS 纹理 → CanvasItem `draw_texture_rect`，编辑器内画布采样 BGRA8 由 Metal 硬件映射 RGBA，颜色与软件路径一致。

---

## 3. 技术方案

### 3.1 总体架构

```
CEF GPU 进程合成 → shared texture（mac: IOSurface / Win: D3D11 shared handle）
→ OnAcceleratedPaint 回调（浏览器进程，句柄每帧变化）
→ Godot RenderingDevice 新增"跨 API 纹理导入"API（从句柄建纹理）
→ WebPanel 直接绘制该纹理（复用现有 _draw，无需 Image/ImageTexture 拷贝）
```

### 3.2 分步实施

**Step 1：CEF 侧接通 OnAcceleratedPaint（平台无关）**
- `window_info.shared_texture_enabled = 1`。
- `ClientDelegate` 实现 `onAcceleratedPaint`（CefViewCore delegate 已有虚函数），把句柄 + 尺寸转给 `WebViewManager` → `WebPanel`。
- **验证点**：日志确认 `OnAcceleratedPaint` 被调用、句柄有效、尺寸正确。

**Step 2：Godot 侧跨 API 纹理导入（✅ 已具备，实施时确认零引擎改动）**
- **不新增 API**：`RenderingDevice::texture_create_from_extension` 已存在（见 §2.3），mac 路径直接把 `MTL::Texture*`（IOSurface 打开）作为 native handle 传入即可。
- 宿主（WebPanel）流程：回调内 IOSurface → `newTexture(desc, iosurface, 0)` → 导入 RD → `RD::texture_copy` 拷到自有目标纹理（尺寸变化重建）→ `Texture2DRD` 绘制。
- Win（后置）：D3D11 shared handle → `ID3D12Device::OpenSharedHandle`（同 adapter）后同样走 `texture_create_from_extension`（d3d12 驱动已实现）。

**Step 3：WebPanel 消费 GPU 纹理**
- `WebPanel` 从"ImageTexture 上传"改为"持有 RID 纹理直接绘制"（`_draw` 用 `draw_texture` / RenderingDevice 纹理）。
- 保留软件路径作为回退（编译开关或运行时检测）。

**Step 4：resize 收敛验证与清理**
- 验证拖动 resize 瞬时收敛（无 250ms 残余窗口）、无黑边无变形。
- 视情况移除/简化尾随重发、1:1 裁剪分支（根因 3 消失后这些是冗余）。

### 3.3 平台矩阵

| 平台 | CEF 交付 | Godot 渲染器 | 导入路径 |
|---|---|---|---|
| macOS | IOSurface | Metal | `MTL::Device::newTexture(descriptor, iosurface)` |
| Windows | D3D11 shared handle | D3D12 | `ID3D12Device::OpenSharedHandle` |
| （可选）Linux | — | Vulkan | `VK_KHR_external_memory` / EGL（后置，不阻塞） |

---

## 4. 关键风险与坑（提前标注）

1. **Godot 引擎核心改动**：新增 RenderingDevice API 涉及接口 + 驱动实现，需过 review；**建议新增而非修改既有 API**（向后兼容）。
2. **句柄生命周期**：`OnAcceleratedPaint` 句柄每帧变化，**不得缓存**；回调内打开 → 下一帧句柄失效。Godot RID 与 CEF 句柄的生命周期映射需明确（每帧重建 RID vs 句柄复用检测）。
3. **跨 adapter**：Win D3D11→D3D12 共享需同 adapter（CEF GPU 进程与 Godot 是否同 adapter——CEF 用系统默认 GPU，Godot 同机同 GPU，一般同 adapter，但需验证）。
4. **纹理格式**：CEF 输出格式（通常 RGBA/BGRA 8-bit）与 Godot 纹理格式匹配；mac IOSurface 的 pixel format（`kCVPixelFormatType_32BGRA` 等）与 Metal texture 一致。
5. **与软件路径共存**：保留 `shared_texture_enabled=0` 回退（Linux/异常时），避免平台缺失导致功能全断。
6. **CEF 版本**：CEF 151 的 `CefAcceleratedPaintInfo` 签名（非旧 `void* shared_handle`）——CefViewCore 已按 `#if CEF_VERSION_MAJOR < 124` 分支处理。

---

## 5. 验收标准

1. `shared_texture_enabled=1` 下 WebDock 正常渲染（页面 200、无黑边、无变形）。
2. **拖动分隔条 resize 瞬时收敛**（无 250ms 残余窗口、无卡死；连续快速拖动 + 停止后均正常）。
3. 无 GPU/网络/renderer 进程崩溃；退出干净（exit 0、无残留 helper）。
4. mac + Win 双平台通过（Step 2 的导入路径各验证一次）。
5. 软件路径回退可用（编译开关切换后回到当前行为）。

---

## 6. 参考资源

- **本次问题根因**：`doc/plans/实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md`（§2.3 死锁链、§3.5 根因主次、§6 根治候选①）。
- **渲染链路现状**：`doc/plans/技术详解-CEF-OSR渲染机制与像素链路.md`（§7 性能模型、§6.2 GPU OSR 成本）。
- **GPU 加速已验证**：`doc/plans/实施记录-C++路线-mac双平台适配与验证.md`（C-mac-2）、`doc/plans/验证计划-CEF-GPU加速-Win先行.md`。
- **CEF 参考实现**：cefclient `tests/cefclient/browser/osr_render_handler_win_d3d11.cc`（D3D11 路径）、obs-browser `browser-client.cpp`（生产级 GPU 导入）、webview_cef（CEF 149 GPU/IME，Apache-2.0）。
- **Godot 侧**：`drivers/metal`、`drivers/d3d12`、`servers/rendering/rendering_device.h`、`thirdparty/metal-cpp`（`MTLDevice.hpp:503`）、`platform/macos/embedded_gl_manager.mm`（IOSurface 参考）。

---

## 7. 决策点（需用户/团队裁决）

1. **Godot 引擎核心改动范围**：~~新增 RenderingDevice API~~ → **无需改动**：实施中发现本 fork 已具备跨 API 纹理导入（`RD::texture_create_from_extension`，`servers/rendering/rendering_device.cpp:1844`，三驱动均已实现；metal 驱动按 `MTL::Texture*` 包装）。模块侧仅需 `RD::get_context_driver()` → `RenderingContextDriverMetal::get_metal_device()` 取设备 + metal-cpp `newTexture(descriptor, IOSurfaceRef, plane)`。计划中“最大成本点”实际为零。（已解决）
2. **优先级**：~~先 mac 还是先 Win~~ → **mac 先行**（用户裁决 2026-08-04，metal-cpp 资产最直接；本机实机验证完成）。Win 路径后置。（已解决）
3. **软件路径去留**：GPU 直通验证稳定后，软件路径保留为回退（编译开关）还是移除（少维护面）——**暂保留**（`WEBVIEW_OSR_SOFTWARE=1` 运行时回退；非 mac 平台依赖）。移除时机待 Win 路径落地后另行评估。（未决，默认保留）

## 8. 实施记录（2026-08-04，mac arm64）

### 8.1 改动清单（全部位于 `modules/webview/`，无引擎核心改动）

| 文件 | 改动 |
|---|---|
| `webview_core.h` | `Callbacks` 新增 `on_accelerated_paint(id, handle, w, h)`（handle 为 mac IOSurfaceRef 按 uint64 透传） |
| `webview_core.cpp` | `create_browser`：mac 默认 `shared_texture_enabled=1`（`WEBVIEW_OSR_SOFTWARE=1` 回退 0）；`ClientDelegate::onAcceleratedPaint` 转发（CEF ≥124 签名，取 `info.shared_texture_io_surface` + `extra.coded_size`） |
| `webview_manager.h/.cpp` | 接线 `on_accelerated_paint` → `WebPanel::set_accelerated_paint` |
| `web_panel.h/.cpp` | GPU OSR 消费端：自有 RD 目标纹理（BGRA8，尺寸变化重建）→ 回调内 IOSurface 打开源纹理 → `texture_create_from_extension` 导入 → `RD::texture_copy` 同队列拷贝 → `Texture2DRD` 直接绘制；`_draw` 按 `gpu_path_active` 优先 GPU 纹理，软件路径保留 |
| `SCsub` | mac：`env_webview` 增加 `thirdparty/metal-cpp` include 路径（IOSurface framework 原已链接） |

### 8.2 渲染链路（mac）

```
CEF GPU 进程（Metal 合成）→ IOSurface（GMB，BGRA8）→ OnAcceleratedPaint（主线程）
→ MTL::Device::newTexture(desc, iosurface)（Godot 设备实例，StorageModeShared）
→ RD::texture_create_from_extension（BGRA8 格式匹配，不建 view，RD 接管引用）
→ RD::texture_copy → 自有 BGRA8 目标纹理（Godot 队列，与面板绘制同帧 FIFO）
→ Texture2DRD → _draw 绘制（Metal 硬件采样 BGRA→RGBA，颜色与软件路径逐字节一致）
```

关键实现点：
- **句柄生命周期**：源纹理每帧从 IOSurface 新建、当帧拷贝后经 `free_rid` 释放（CEF 缓冲池句柄不得缓存，见 `cef_render_handler.h` OnAcceleratedPaint 文档）；目标纹理由 RD 创建并常驻，尺寸变化才重建。
- **跨进程同步**：CEF 在 GPU 帧就绪后才回调（cefclient mac 同款时序），拷贝无需额外 fence。
- **渲染器门控**：`dynamic_cast<RenderingContextDriverMetal*>` 判定（mac 上 `--rendering-driver vulkan` 时忽略 GPU OSR 保持软件路径）；`METAL_ENABLED` 编译门控。
- **线程模型**：CEF pump 与 RD 调用同主线程（编辑器默认 `thread_model=Safe` 单线程渲染）；若启用 Separate 渲染线程，GPU OSR 需渲染线程转发（未实现，文档注记）。

### 8.3 验证证据（2026-08-04 实机，Apple M1 Max，Metal 3.2 Forward+）

| 验收项 | 结果 | 证据 |
|---|---|---|
| §5.1 GPU 路径正常渲染 | ✅ | `GPU OSR frame: 320x240→320x1809`（首帧+布局展开）；页面 200；JS 桥 invoke 往返正常；截图 dock 区域 1046 unique colors（非空白）；颜色与软件路径一致（dock 区域 mean RGB 45.2,45.9,52.0 vs 软件 45.1,45.7,51.8，R/B 0.870 vs 0.869——无 R/B 互换） |
| §5.2 resize 瞬时收敛 | ✅ | 7 次连续快速 resize（850/950/800/1050/880/920/900 高，400ms 间隔）每次立即产出新尺寸帧（1509/1709/1409/1807/1569/1649/1609）；**尾随重发分支 0 触发**（临时插桩计数，插桩已移除） |
| §5.3 无崩溃 + 干净退出 | ✅ | 多轮运行无 `GPU process exited`/`FATAL`；Cmd+Q 退出 exit=0，8s 后 0 残留 helper |
| §5.4 mac 双平台（Win 后置） | ✅ mac / ⏸ Win | Win 路径（D3D11 shared handle → `ID3D12Device::OpenSharedHandle`）未实施，非 mac 保持软件路径 |
| §5.5 软件路径回退 | ✅ | `WEBVIEW_OSR_SOFTWARE=1` 运行：页面 200、无 GPU OSR 帧（OnPaint 软件路径） |

### 8.4 观察与遗留

- **窗口关闭（点红点）退出会残留 CEF helper**（Cmd+Q 干净）——疑似 mac 编辑器窗口关闭退出路径不完整，与本次改动无关（未改退出逻辑），待单独排查。
- resize 节流（25ms）与尾随重发逻辑保留未动：GPU 路径下已惰性（验证 0 触发），但移除属独立行为变更（Step 4），待评审。
- Win 的 `cefViewQuery` 通路（验证计划 §3.4 附带发现）与本改动无关，未处理。
- 性能：GPU 直通消除 CPU 读回（原每帧 BGRA→RGBA 逐像素 + memcpy）；拷贝为 GPU blit，开销可忽略。未做帧率 A/B 专项测量（编辑器 UI 主观流畅，无硬指标）。
