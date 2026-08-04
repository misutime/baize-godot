# 实施计划：CEF 源码修改——根治 OSR resize 收敛死锁（交接文档）

> **状态**：交接中（2026-08-04）。目标：由同事修改 CEF 源码（`libcef/browser/osr/` 的 OSR 视图逻辑），从源头修复"resize 收敛死锁"，使软件/GPU 两条交付路径都不再依赖宿主侧兜底。
>
> **前置阅读**：
> - 《实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md》（根因 3 完整死锁链）
> - 《技术详解-GPU-OSR与帧调度-概念澄清与解决路径.md》（三概念：交付方式/帧源/损伤驱动；实测 vs 推断边界）
> - 《实施计划-GPU-OSR纹理直通-根治resize死锁.md》（GPU 路径已实现并实测有效，§8 是本修改的"对照实验"参考）

---

## 1. 前因：问题全貌

WebDock（CEF OSR 面板，软件读回 `shared_texture_enabled=0`）拖动分隔条改变宽度时，出现三层问题：

1. **黑边**：CEF 未设 `background_color` 时页面透明区域 alpha=0，显示露出原生底色。
2. **变形/错位**：旧 `TextureRect` 显示控件尺寸被纹理 minimum_size 钳制，与面板不一致。
3. **收敛死锁（核心）**：resize 后 CEF 渲染器不产出新尺寸帧，内容卡在旧尺寸。

1/2 已修复（宿主侧：直接 `_draw` + 1:1 裁剪；`c565c5d1b6`）。3 的宿主侧修复（节流 + 尾随重发 ≤250ms 收敛）**有效但非根治**——存在残余窗口。

**为什么还要改 CEF**：死锁根因位于 **CEF 自身的 OSR hold 逻辑缺陷**（详见 §2）。宿主侧所有手段（节流、重发、GPU 直通）都是"绕过/刺激"，无法从机制上消除——且 GPU 路径有效的原因至今未深挖（§4），在 CEF 源码层面修改可以：
- 软件路径也根治（不依赖 GPU 直通，Linux/异常场景受益）
- 弄清"GPU 路径为什么有效"的机制，验证修改方案的正确性

---

## 2. 死锁机制（前因核心，源码行号）

参考源码：`refers/cef/libcef/browser/osr/render_widget_host_view_osr.cc`（CEF 151）。

### 2.1 关键代码路径

```cpp
// WasResized（1087-1098）：hold 激活期间，后续 resize 只记 pending，不同步
void WasResized() {
  if (hold_resize_) { pending_resize_ = true; return; }
  SynchronizeVisualProperties(...);
}

// SynchronizeVisualProperties（1100-1133）→ ResizeRootLayer（1791-1807）
bool ResizeRootLayer() {
  if (!hold_resize_) {
    if (SetRootLayerSize(false)) {        // ← 更新 compositor size（含 SetViewBounds）
      hold_resize_ = true;                //   并激活 hold
      cached_scale_factor_ = GetDeviceScaleFactor();
      return true;
    }
  } else if (!pending_resize_) {
    pending_resize_ = true;               // ← hold 期间：只记 pending，不更新 size
  }
  return false;
}

// OnPaint（1607-1644）：hold 释放条件 = 渲染尺寸 == 期望尺寸
if (hold_resize_) {
  gfx::Size expected = ScaleToCeiledSize(GetViewBounds().size(), cached_scale_factor_);
  if (pixel_size == expected) { ReleaseResizeHold(); }  // 1809-1819
}

// InvalidateInternal（1884-1892）：Invalidate 输出 host_display_client 的旧 pixel_size_
OnPaint(bounds, host_display_client_->GetPixelSize(), ...);

// host_display_client_osr.cc（73-96）：pixel_size_ 只在 OnAllocatedSharedMemory（viz 分配共享内存）时更新
```

### 2.2 死锁链（软件路径）

```
resize(W) → WasResized → ResizeRootLayer（compositor size=W, hold=true）
→ 页面静止时合成器按需不产帧（internal 帧源，damage-driven）
→ viz 不分配 W 尺寸共享内存 → host_display_client.pixel_size_ 停留旧值（W0）
→ Invalidate/合成器输出 W0 帧 → OnPaint(W0) ≠ expected(W)
→ hold 永不释放 → 后续 resize 全部 pending → compositor size 永不更新 → 死锁
```

**本质**：`ResizeRootLayer` 的"仅无 hold 时更新 compositor size"是死锁的**结构性原因**——一旦 hold 激活且合成器未及时产帧，尺寸更新就永久停止，形成循环依赖（更新尺寸→产帧→释放 hold→才允许再更新尺寸）。

---

## 3. 修改目标

**让"resize 事件"在 OSR 视图内必然被及时处理**——具体两条（可只做其一，推荐先做 A）：

### 方案 A（推荐）：`ResizeRootLayer` 在 hold 期间也同步 compositor size

修改 `ResizeRootLayer`（1791-1807）的 `else` 分支：hold 激活期间，**每次收到新尺寸也执行 `SetRootLayerSize`**（更新 compositor size），仅保留 `pending_resize_` 用于"hold 释放后补一次完整同步"。

```cpp
bool ResizeRootLayer() {
  // 无条件尝试更新 compositor size（hold 期间也同步——修复死锁核心）
  const bool size_changed = SetRootLayerSize(false /* force */);
  if (!hold_resize_) {
    if (size_changed) {
      hold_resize_ = true;
      cached_scale_factor_ = GetDeviceScaleFactor();
    }
  } else {
    pending_resize_ = true;   // 保留：hold 释放后再完整同步一次
  }
  return size_changed;
}
```

**为什么这样改**：
- 合成器始终知道最新尺寸 → resize 后即使页面静止，合成器持有新尺寸（配合 Invalidate/提交即产出新尺寸帧）
- `pending_resize_` 保留，hold 释放路径（ReleaseResizeHold → WasResized）不变，无回归风险
- **注意**：`SetRootLayerSize` 内部调用 `SetScreenInfo`/`SetViewBounds`，两者有 `DCHECK(!hold_resize_)`（1726、1755）——**方案 A 需处理此约束**：要么拆开（仅 `SetViewBounds` + `SetScaleAndSize` 在 hold 期间执行，`SetScreenInfo` 跳过），要么调整 DCHECK。**实现时以不触发 DCHECK 为准**。

### 方案 B：`WasResized` 直接强制提交

`WasResized`（1087-1098）在 SynchronizeVisualProperties 后追加一次 `Invalidate(PET_VIEW)` 或直接触发合成器提交（CEF 内部可调用合成器 API，比宿主侧 `CefBrowserHost::Invalidate` 有力——宿主侧 Invalidate 走 `InvalidateInternal` 输出旧 pixel_size_，CEF 内部可直接请求合成器提交新尺寸帧）。

### 方案 C（兜底）：`ReleaseResizeHold` 增加超时/强制释放

`ReleaseResizeHold`（1809-1819）——若 OnPaint 长时间未达期望（如 >500ms），强制释放 hold 并重触发一次 `WasResized`（打破循环依赖）。此方案是"兜底"，不解决"尺寸更新被 hold 阻塞"的结构问题，仅保证最终收敛。

---

## 4. 对照实验：GPU 路径为什么有效（修改前必读）

**GPU 直通（`shared_texture_enabled=1`，mac）已实测有效**（《实施计划-GPU-OSR纹理直通》§8）：7 次 resize 全部瞬时收敛、宿主尾随重发 0 触发。但**机制未深挖**，两个候选解释：

1. GPU 路径的 `Invalidate(PET_VIEW)` 触发合成器**真提交**（新尺寸 GPU 纹理随提交分配）——软件路径 Invalidate 输出旧 `pixel_size_` 才失效；
2. GPU 路径下 viz 在 compositor size 变化时**自动重分配 GPU 纹理并提交**（不需要额外产帧请求）。

**建议**：修改 CEF 前，先用 GPU 路径实测定位到底是哪条（在 `ResizeRootLayer`/`OnAcceleratedPaint` 加日志看提交触发点）——**这直接验证方案 A/B 的正确性**（若解释 2 成立，方案 A 必然有效；若解释 1 成立，方案 B 或 A+Invalidate 组合有效）。

---

## 5. 构建与验证

### 5.1 构建 CEF（重活，提前规划）

- CEF 源码在 `refers/cef`（CEF 151.3.12，Chromium 151）。构建需 depot_tools + automate 流程，**首次构建数小时~天级、磁盘数十 GB**（官方文档：bitbucket.org/chromiumembedded/cef/wiki/BranchesAndBuilding）。
- 目标产物：`libcef.dylib`（mac）/ `libcef.dll`（Win）+ wrapper 库，替换 `bin/cef-dist/` 中现有 CEF 运行时。
- 建议：**先在本机 `refers/cef` 源码层面完成修改 + 静态推演**（改法与 §3），确认逻辑后一次性构建验证，避免反复构建。
- 构建产物替换路径：`bin/cef-dist/151.3.12+gd9cea67+chromium-151.0.7922.47/`（现有 SDK）；stage 脚本 `misc/scripts/stage_webview.py` 的 CEF 版本哨兵/指纹需同步（参考《实施记录-C++路线-mac双平台适配与验证.md》构建选项指纹机制）。

### 5.2 验证步骤（修改 CEF 后，软件路径应无宿主侧兜底也即时收敛）

1. **软件路径**（`WEBVIEW_OSR_SOFTWARE=1`）：
   - 拖动分隔条连续快速 resize → 停止后**即时收敛**（无 250ms 窗口）——即"尾随重发分支 0 触发"（宿主侧尾随重发可临时插桩观察）。
   - 页面静止时 0 帧（damage-driven 保持，惰性不变）。
2. **GPU 路径**（默认）：回归——仍即时收敛、颜色一致、无崩溃。
3. **回归面**：页面加载、JS 桥、IME、窗口 resize、dock 布局调整、退出干净（exit 0、无残留 helper）。
4. **长期**：宿主侧尾随重发/1:1 裁剪是否可简化（死锁根治后为冗余，属独立行为变更，另行评审）。

---

## 6. 风险与注意

1. **fork 维护成本**：修改 vendored CEF 后，升级 CEF 版本需 rebase 补丁（改动点极小——`ResizeRootLayer`/`WasResized` 一处逻辑，rebase 成本低）。建议改动处加醒目注释（`// baize fork: ...`）便于升级时识别。
2. **构建链**：CEF 构建极重，建议 CI/脚本化；先用 GPU 路径实测定位机制（§4）降低返工风险。
3. **DCHECK 约束**：方案 A 需避开 `SetScreenInfo`/`SetViewBounds` 的 `DCHECK(!hold_resize_)`（§3-A 注意）。
4. **行为边界**：不要改变"静态页面 0 帧"的惰性语义（damage-driven 是 CEF/Chromium 的正常行为，本文只修"resize 被阻塞"），避免影响性能基线。
5. **双平台**：修改在 OSR 视图层（平台无关），Win 软件路径同样受益；但 Win 的 GPU 路径（D3D11→D3D12）未实施，软件路径验证以 mac 为准，Win 后置。

---

## 7. 参考

- **死锁源码**：`refers/cef/libcef/browser/osr/render_widget_host_view_osr.cc`（WasResized 1087、ResizeRootLayer 1791、OnPaint 1607、ReleaseResizeHold 1809、InvalidateInternal 1884、SetFrameRate 1695）、`host_display_client_osr.cc`（OnAllocatedSharedMemory 73、Draw 98）。
- **根因文档**：`doc/plans/实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md`（§2.3、§3.5）。
- **概念澄清**：`doc/plans/技术详解-GPU-OSR与帧调度-概念澄清与解决路径.md`。
- **GPU 对照**：`doc/plans/实施计划-GPU-OSR纹理直通-根治resize死锁.md`（§8 实测）。
- **CEF 构建**：官方 BranchesAndBuilding wiki（depot_tools 流程）；本仓库 `misc/scripts/cef_dist.py`（SDK 下载/校验/哨兵）。
