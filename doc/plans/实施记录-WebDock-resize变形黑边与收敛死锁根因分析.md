# WebDock resize 变形/黑边/收敛死锁——根因分析与修复记录

> 状态：已修复并实测通过（2026-08-04）。涉及 `modules/webview/` 的 WebPanel 渲染与 resize 链路。
> 本文记录完整根因链（含 CEF 源码证据）、最终保留的修复、被否决的方案与原因，供后续回顾。

## 1. 问题现象（按发现顺序）

拖动编辑器左侧 WebDock（CEF OSR 面板）分隔条改变宽度时，依次暴露三层问题：

1. **文字横向压缩变形**：拖动中及停止后，内容被非等比拉伸（宽度变、高度不变），文字压扁。
2. **黑色未填满区域**：面板四周出现黑色空隙（原生 dock 背景），内容"居中"或被截断；随拖动恶化。
3. **收敛卡死**：停止拖动后内容卡在旧尺寸（1:1 显示 + 深蓝空隙），1 秒后（超时机制）或永不恢复。

## 2. 根因链（三层，均源码证实）

### 2.1 黑边 = CEF buffer 透明区域（已修复：`background_color`）

- CEF OSR（`windowless_rendering_enabled=1`）未设 `CefSettings.background_color` 时，页面未覆盖区域 **alpha=0（透明）**。
- 纹理拉伸显示时透明区域露出原生黑色背景。
- **证据**：八点 RGBA 采样，页面加载首帧 `TL(0,0,0,a0)`（四角全透明）、四边中点 `(34,34,51,a255)`（#223 页面背景）——buffer 内内容不满。
- **验证**：八点 RGBA 采样证实黑边根因是透明区域（首帧 `a0`）；页面加载完成后 body 背景不透明填满（全部 `a255`），无透明区域。
- **最终处理：不设 `background_color`**。页面背景是页面自身职责（`index.css` body 背景不透明 + `height:100%` 填满视口即无透明区域）；固定 CEF 底色（曾试 `0xFF222233`）与未来动态主题冲突，且页面加载完成后底色本就不可见——已去除，仅加载瞬态可能短暂露黑（页面加载完成即覆盖）。

### 2.2 变形/黑边错位 = TextureRect 显示策略（已修复：WebPanel 直接 `_draw`）

- 旧实现用 `TextureRect` 子控件（`STRETCH_SCALE` + `PRESET_FULL_RECT` 锚点）显示 OSR 纹理。
- **机制**：`TextureRect` 默认 `expand_mode = EXPAND_KEEP_SIZE`（texture_rect.h:61-63），`get_minimum_size()` 返回**纹理尺寸**（texture_rect.cpp:128-133），且 Control 的 `_size_changed` 会把锚点计算出的 rect **向上钳制到 minimum_size**（control.cpp:2224-2262）——**子控件尺寸被旧纹理尺寸撑大**，与面板不一致 → 纹理拉伸到错误区域 → 变形/露出面板底色。
- 后续尝试 `set_rect` 显式布局仍被 minimum_size 机制覆盖（日志：`texrect` 恒滞后于面板）。
- **修复**：**删除 TextureRect，WebPanel 在 `NOTIFICATION_DRAW` 直接 `draw_texture_rect`**——绘制区域恒等于 WebPanel 自身 rect（position 0,0、尺寸=get_size()），无子控件布局依赖。
  - 纹理尺寸 == 面板：精确全幅。
  - 纹理 ≠ 面板（resize 未收敛窗口）：**1:1 左上裁剪**——无变形；余区不填充，露出面板原生底色（用户定案：问题暴露时黑边是可视诊断信号，且避免硬编码背景色与主题冲突）。

### 2.3 收敛卡死 = CEF OSR hold 机制 + 合成器按需不产帧（已修复：节流 + 尾随重发）

**CEF 源码机制**（refers/cef，`libcef/browser/osr/render_widget_host_view_osr.cc`）：

- `WasResized()`（1087-1098）：`hold_resize_` 期间只记 `pending_resize_`，不同步。
- `SynchronizeVisualProperties → ResizeRootLayer`（1791-1807）：**仅"无 hold"时**执行 `SetRootLayerSize`（更新 compositor 尺寸）+ 设 `hold_resize_=true`；此后 resize 全部 pending。
- `OnPaint`（1607-1644）：`pixel_size == ScaleToCeiledSize(GetViewBounds().size(), cached_scale_factor_)` **才 `ReleaseResizeHold`**（1809-1819）。
- `InvalidateInternal`（1884-1892）：`OnPaint(bounds, host_display_client_->GetPixelSize(), ...)`——**输出的是 host_display_client 的 pixel_size_**。
- `host_display_client_osr.cc`（73-96）：`pixel_size_` **只在 `OnAllocatedSharedMemory`（viz 合成器分配共享内存时）更新**。

**死锁链**：

```
resize(W) → WasResized → ResizeRootLayer（更新 compositor size=W, hold=true）
→ 页面静止时合成器按需不产帧（internal 帧源）→ viz 不分配 W 尺寸共享内存
→ host_display_client.pixel_size_ 停留旧值（W0）
→ Invalidate/合成器输出 W0 帧 → OnPaint(W0) ≠ expected(W) → hold 永不释放
→ 后续 resize 全部 pending → 合成器 size 永不更新 → 死锁
```

**实测证据**（饱和日志）：`manager resize 642 → set_size 642 → getViewRect 642 ×2 → onPaint 卡 339` 长达 1 秒，仅超时重发后才输出 642/825。

**修复**：
- `Invalidate(PET_VIEW)`（cefclient `osr_render_handler OnResize` 同款配套，was_resized 后强制整幅重绘请求）。
- **节流下发**（`NOTIFICATION_PROCESS`，25ms 合并）：拖动中不逐帧 resize，只记 `pending_size_`。
- **尾随重发**：已下发尺寸（`applied_size_`）未渲染出超过 1 秒 → **重发同尺寸** WasResized+Invalidate，强制合成器活动加速收敛；**不改 GetViewBounds 为其他尺寸**（避免死锁：旧 surface 永远 ≠ 新期望）。

## 3. 被否决的方案（避免重复踩坑）

| 方案 | 结论 | 原因 |
|---|---|---|
| `TextureRect` 显式 `set_rect` 布局 | 无效 | minimum_size 钳制覆盖（2.2） |
| **external begin frame**（`external_begin_frame_enabled=1` + 每帧 `SendExternalBeginFrame`） | **无效** | 软件渲染路径（`shared_texture_enabled=0`）下 viz 不触发 `Draw`——OnPaint 完全无输出（实测 8 秒零帧）。external BF 主要面向 GPU 路径 |
| ACK 串行化（一次一个 inflight resize，OnPaint ack 后发下一个） | 不彻底 | CEF 死锁时 ack 永不发生；且"清 inflight 保险"会改 GetViewBounds 违反串行化（shifu 验证） |
| 1 秒超时清 inflight 保险 | 有缺陷 | pending 变更时改最新 GetViewBounds → 死锁再现（shifu 验证） |
| contain（保持纵横比居中）显示 | 否决 | 拖动中产生巨幅留白（如 505→320 面板时缩成 320×1146） |
| 简单 stretch（`draw_texture_rect` 拉伸） | 否决 | 压字变形 |

## 3.5 根因主次关系（2026-08-04 定案）

**根因 3（CEF hold 收敛死锁）是核心根因；根因 1、2 是其衍生**（纹理与面板不一致的后果）：

- 若根因 3 彻底解决（纹理实时 = 面板）：根因 2（变形/错位）完全消失（恒走精确全幅）；根因 1（黑边）的主要表现（纹理≠面板余区）消失，仅剩纹理内透明（页面自身背景职责）。
- **但当前约束（软件 OSR，不改 GPU）下根因 3 只能缓解不能根治**——CEF 合成器按需产帧是行为限制，宿主侧只能尾随重发加速收敛（≤250ms）。
- 因此根因 1、2 的修复（1:1 裁剪防变形、不填充底色让问题可见）是**残余窗口（≤250ms 未收敛期）的必需兜底**，非多此一举。
- **真正的根治路径**（二选一，均属大改，暂缓）：① GPU OSR（`shared_texture_enabled=1` + `OnAcceleratedPaint` + Metal/D3D 导入）；② **fork CEF 修改 hold 机制**（在 `ResizeRootLayer`/`ReleaseResizeHold` 逻辑中修复“hold 期间 compositor 尺寸不更新”的死锁——见 §2.3 源码证据，作为未来机会点，另行评估）。

## 4. 最终保留的修复（当前代码）

`modules/webview/`：

1. **`webview_core.cpp`**：
   - `CefSettings.background_color = 0xFF222233`（透明黑边根治）。
   - `resize_browser`：`WasResized()` + `Invalidate(PET_VIEW)`。
   - `window_info.external_begin_frame_enabled = 0`（internal 帧源，external 软件路径无效）。
2. **`web_panel.cpp`**：
   - `NOTIFICATION_DRAW`：直接绘制，纹理==面板时全幅、否则 **1:1 左上裁剪**（不填充底色）。
   - `sync_size`：只记录 `pending_size_`（desired），创建时立即 create_browser。
   - `NOTIFICATION_PROCESS`：**25ms 节流下发** pending；**未收敛 >1 秒尾随重发同尺寸**。
   - `set_paint`：更新 `last_paint_size_`（收敛基线）。

## 5. 影响范围

- **触发条件**：任何导致 WebPanel 尺寸变化的场景——拖动 dock 分隔条（高频，主暴露面）、编辑器窗口 resize、dock 布局调整（开关/移动 dock、切换工作区）、窗口最小化恢复。
- **不受影响**：场景打开/切换、字体/缩放设置变化、页面交互（不改变面板尺寸）。
- 高频拖动为主要问题源；低频单次 resize 由尾随重发兜底（停止后 ≤250ms 收敛）。

## 6. 验证与遗留

- 实测通过：拖动无变形、无黑边、停止后 ≤1 秒收敛；启动/布局正常。
- **遗留**：CEF 软件 OSR 的 resize 收敛有**固有延迟**（合成器按需产帧是 CEF 行为限制，宿主侧无法消除）——当前“无变形 + 露出底色信号 + ≤250ms 收敛”是宿主侧最优。
- **根治候选（暂缓，待评估）**：
  1. **GPU OSR**（`shared_texture_enabled=1` + `OnAcceleratedPaint` + Metal/D3D 导入）——合成器持续活跃、resize 可瞬时收敛（V2 路线）。
  2. **fork CEF 修改 hold 机制**（见 §3.5）——从源头修复“hold 期间 compositor 尺寸不更新”的死锁；需评估 fork 维护成本。
