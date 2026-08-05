# 技术详解：GPU OSR 与帧调度——概念澄清与解决路径

> **状态**：2026-08-04 初稿；**2026-08-05 修正**（§3.1-4 / §3.2 / §3.3 / §4 / §5）：mac 的"GPU 路径 resize 瞬时收敛"降级为**未证实观察**——新增源码核实结论（§3.3）：hold 死锁**路径无关**（OnPaint 与 OnAcceleratedPaint 释放检查逐行相同、共用同一 video capturer），GPU 直通只改交付、不根治死锁；fork CEF 恢复为必要项。本文澄清 WebDock resize 死锁解决路径上的概念混淆（曾出现"GPU OSR 能持续产帧"的错误推断），建立准确的概念模型，并给出按确定性排序的解决路径与验证顺序。
>
> **前置阅读**：《实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md》（根因 3：CEF hold 死锁）、《实施计划-GPU-OSR纹理直通-根治resize死锁.md》（交接文档，部分前提被本文修正）。

---

## 1. 为什么写本文

在探索"彻底解决 resize 死锁"的过程中，出现过两个方向的混淆，导致路线判断反复：

1. **"60 帧/秒持续驱动"方案**（external begin frame）在软件路径实测失效后被废弃——但它的失效是否**只限于软件路径**、在 GPU 路径是否有效，**从未验证**。
2. **"GPU OSR 能根治死锁"**的说法（交接文档沿用）——基于"GPU 路径合成器持续活跃"的**错误推断**——GPU OSR 只改变像素交付方式，**不改变帧调度**。

本文把三个独立概念拆开、区分**实测事实与推断**、给出确定性排序的解决路径，避免后续再次混淆。

---

## 2. 三个独立概念（混淆的根源）

WebDock 的渲染链路里，有三个**相互独立**的开关/机制，各自解决不同问题：

| # | 概念 | 对应代码/机制 | 作用 | 是否影响"resize 后是否产帧" |
|---|---|---|---|---|
| A | **像素交付方式** | `window_info.shared_texture_enabled`（0=软件读回 OnPaint，1=GPU 纹理直通 OnAcceleratedPaint） | 决定像素以 CPU 拷贝还是 GPU 句柄交付给宿主 | **否** |
| B | **帧源** | `window_info.external_begin_frame_enabled`（0=internal 合成器自主，1=external 宿主驱动）+ 宿主 `SendExternalBeginFrame` | 决定合成器"由谁催着产帧"（自主按需 vs 宿主 60fps 驱动） | **是**（external 强制产帧） |
| C | **损伤驱动（damage-driven）** | Chromium 合成器固有行为：只在有 damage（内容/尺寸/滚动变化）时提交帧 | 决定"静态页面是否有帧产出" | **是**（无 damage 不产帧） |

**关键结论**：
- **A（GPU OSR）与 C（damage-driven）正交**——无论软件还是 GPU 交付，合成器都是按需提交。**GPU OSR 不改变"resize 后是否产帧"**。
- **B（external 帧源）是唯一能强制产帧的开关**——但它的有效性可能依赖交付方式（见 §3）。

---

## 3. 实测事实 vs 推断（明确边界）

### 3.1 实测事实（有日志/运行证据）

1. **软件路径死锁链成立**（饱和日志 + CEF 源码逐行）：
   `resize → hold_resize_ 激活 → 静态页合成器按需不产帧 → viz 共享内存尺寸不更新（pixel_size_ 旧值）→ Invalidate 输出旧尺寸帧 → OnPaint ≠ 期望 → hold 永不释放`。
   （证据见《实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md》§2.3）
2. **external begin frame 在软件路径实测失效**：`external_begin_frame_enabled=1` + 每帧 `SendExternalBeginFrame` → **OnPaint 完全无输出（8 秒零帧）**，UI 卡死。原因：软件渲染路径下 viz 不触发 `CefLayeredWindowUpdaterOSR::Draw`（host_display_client_osr.cc 的软件输出链未随外部帧驱动）。
3. **宿主侧修复有效但非即时**：节流 + 尾随重发 → 无变形、无黑边、≤250ms 收敛（已实测通过，`c565c5d1b6`）。
4. **GPU OSR 单独实现（mac）实测：resize 即时收敛**（2026-08-04 实机：7 次连续快速 resize 全部瞬时产出新尺寸帧、尾随重发分支 0 触发——见《实施计划-GPU-OSR纹理直通》§8）。**“拖动中视觉与宿主侧修复差别不大”**源于显示策略（1:1 裁剪 + 露底色）未变、且拖动中纹理仍需一帧渲染——**不是**收敛机制未变。**修正 §2 结论 A**：GPU 路径下 resize 能及时触发合成器提交新尺寸帧（帧提交语义与软件不同），但“合成器持续活跃”仍不成立（damage-driven 不变，静止仍 0 帧）。**⚠️ 2026-08-05 再修正**：本条为 mac 单机观察、机制未证实——源码推演（§3.3）显示 hold 释放检查在 OnPaint 与 OnAcceleratedPaint 逐行相同，GPU 直通不提供结构性免死锁。本条降级为“未证实观察”，不得作为设计依据。

### 3.2 推断 / 未验证（不得当作事实引用）

| 说法 | 状态 | 依据 |
|---|---|---|
| "GPU OSR 合成器持续活跃，从机制上消除死锁" | **错误推断，已废弃** | Chromium 合成器无论 GPU/软件均 damage-driven（§2-C）；GPU 直通只改交付（§2-A）。交接文档 §3.2 曾沿用此说，需以本文为准 |
| “GPU 路径下 resize 能及时触发合成器提交新尺寸帧” | **mac 单机观察，机制未证实（2026-08-05 降级）** | mac 7 次 resize 瞬时收敛（《实施计划-GPU-OSR纹理直通》§8）；但源码推演（§3.3）两条交付路径共用同一 video capturer（video_consumer_osr.cc:37-57）、hold 释放检查逐行相同（render_widget_host_view_osr.cc:1640/1677）——GPU 直通不改帧调度，观察无机制支撑；Windows 必须实测 |
| "external begin frame 在 GPU 路径有效" | **未验证** | 软件路径失效（§3.1-2）；GPU 路径是 cefclient 的标准组合（GPU + external BF），但本工程未测 |

### 3.3 源码核实结论（2026-08-05，refers/cef CEF 151，逐行核实）

| 事实 | 源码位置 | 结论 |
|---|---|---|
| `shared_texture_enabled=1` 时 OnPaint 不被调用，改走 OnAcceleratedPaint | cef_render_handler.h:140-141 | GPU 直通 = CEF 侧零 CPU 读回（交付契约，确定收益） |
| Windows 上 OnAcceleratedPaint 交付 DXGI 共享句柄，像素留 GPU | video_consumer_osr.cc:174-177（`gmb_handle.dxgi_handle().buffer_handle()`） | 同上 |
| 软件路径交付 CPU 像素（共享内存映射） | video_consumer_osr.cc:232-244 | 每帧 CPU 读回真实存在 |
| 两条路径共用同一 `viz::ClientFrameSinkVideoCapturer`，合成器提交什么捕获什么 | video_consumer_osr.cc:37-57 | **帧调度路径无关**：damage-driven、静态页 0 帧对两条路径相同 |
| `ResizeRootLayer`：无 hold 才同步尺寸，hold 期间只置 pending | render_widget_host_view_osr.cc:1791-1806 | hold 机制存在（§2.1 死锁链成立） |
| hold 释放检查在 `OnPaint`（:1640-1641）与 `OnAcceleratedPaint`（:1677-1678）**逐行相同**——pixel_size == expected 才释放 | render_widget_host_view_osr.cc | **死锁路径无关**：GPU 路径在“静态页 + resize”下同样 hold 不释放 |
| `Invalidate` → `RequestRefreshFrame`，输出合成器当前尺寸帧 | render_widget_host_view_osr.cc:1163-1166；video_consumer_osr.cc:107-110 | 旧尺寸帧 → hold 不释放（两条路径一致） |

**由此得出的硬结论**：
- “GPU 路径能根治死锁”**不成立**（源码推演反例：静态页 + resize + GPU 直通 → hold 同样不释放）。
- mac 的“瞬时收敛”观察（§3.1-4）是未验证现象，[INFERENCE] 可能受测试页面持续动画等混淆因素影响（未调查）；**Win 上必须实测，不得作为设计依据**。
- GPU 直通的确定收益只有“零 CPU 读回”（交付契约）；resize 死锁的根治手段是《实施计划-CEF源码修改-根治resize收敛死锁.md》方案 A/B（§4-①）。

---

## 4. 解决路径（按确定性排序）

目标是：**resize 后合成器必然及时产帧（即时收敛），静止时零成本（惰性）**。三条路：

| 路径 | 机制 | 确定性 | 成本 | 备注 |
|---|---|---|---|---|
| **① fork CEF 改 hold/产帧逻辑** | CEF 内部修 `ResizeRootLayer`（hold 期间也同步 compositor size）或 `WasResized`（resize 后强制合成器提交） | **最确定**——不依赖交付方式（A）与帧源（B），软件/GPU 都根治 | 小改动（一处逻辑）+ fork 维护成本 | 直达根因（CEF 自身缺陷） |
| **② GPU OSR + external begin frame 组合** | `shared_texture_enabled=1` + `external_begin_frame_enabled=1` + 宿主持续 `SendExternalBeginFrame`——cefclient 标准组合 | **待验证**（半天 spike） | 小改动 + 已有 GPU 实现 | 若 GPU 合成器正确处理外部帧（§3.2-3），可实现"即时 + 惰性？（external 是持续驱动，静止也产帧——"惰性"打折，但 GPU 直通下开销可控）" |
| **③ GPU OSR 单独（已实现，mac）** | 只改交付（A）：消除 CPU 读回（确定收益，§3.3 契约）；**不改变帧调度、不提供结构性免死锁**（§3.3） | mac 上“瞬时收敛”为**未证实观察**（§3.1-4），Win 需实测；死锁根治仍依赖 ① | 已实现 | 性能向：零 CPU 读回；拖动中视觉（1:1 裁剪）不变是显示策略所致 |

---

## 5. 验证进度（2026-08-05 更新）

**路径③（GPU OSR 单独）mac 已实现，但"resize 即时收敛"是未证实观察（§3.1-4 / §3.3）**——不能据此宣称死锁已解决。验证顺序调整：

- 步骤 3（fork CEF ①）——**恢复为必要项**：源码推演（§3.3）确认 hold 死锁在软件/GPU 两条交付路径都存在（释放检查逐行相同），fork 修改（《实施计划-CEF源码修改-根治resize收敛死锁.md》方案 A/B）是唯一根治手段。
- 步骤 1（加日志验证 GPU 路径收敛机制）——**重新需要**：mac 观察未证实；Win GPU 路径落地后须实测（Invalidate 是否真提交 vs 其他混淆因素）。
- 步骤 2（组合 ② external begin frame）——保持不做：external BF 仅当需要“强制持续产帧”（如页面动画在 GPU 路径异常）时再评估。

**决策点**：fork CEF 落地前，宿主侧节流 + 尾随重发（≤250ms 收敛）继续作为唯一防线（现状不变）。

---

## 6. 与现有文档的关系

- **修正**：《实施计划-GPU-OSR纹理直通-根治resize死锁.md》§2.3/§3.2 中"GPU 路径合成器持续活跃，从机制上消除死锁"的说法——**以本文 §3.2 为准**（错误推断）。该文档的 Step 1-4 实施步骤本身不受影响（CEF 侧 + Godot 侧工程不变），但**验收标准 2"resize 瞬时收敛"的达成依赖路径②或①**，非 GPU OSR 单独。
- **沿用**：《实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md》§2.3（死锁链）与 §6（遗留）——其中"GPU OSR 为根治候选①"需按本文调整为"fork CEF 为候选①，GPU OSR 降为候选②（性能向）"。

---

## 7. 一句话总结

**GPU OSR 解决"像素送得快"（零 CPU 读回，交付契约），external 帧源解决"催合成器产帧"（60fps 驱动），damage-driven 是合成器的固有行为（两者之下都按需）——"resize 即时收敛"需要的是"强制产帧"（① fork CEF 或 ② GPU+external 组合），GPU OSR 单独不提供它。**（源码核实：hold 死锁路径无关，见 §3.3；mac 观察未证实，见 §3.1-4）
