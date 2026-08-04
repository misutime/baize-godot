# 技术详解：GPU OSR 与帧调度——概念澄清与解决路径

> **状态**：2026-08-04。本文澄清 WebDock resize 死锁解决路径上的概念混淆（曾出现"GPU OSR 能持续产帧"的错误推断），建立准确的概念模型，并给出按确定性排序的解决路径与验证顺序。
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
4. **GPU OSR 单独实现（mac）实测：resize 即时收敛**（2026-08-04 实机：7 次连续快速 resize 全部瞬时产出新尺寸帧、尾随重发分支 0 触发——见《实施计划-GPU-OSR纹理直通》§8）。**“拖动中视觉与宿主侧修复差别不大”**源于显示策略（1:1 裁剪 + 露底色）未变、且拖动中纹理仍需一帧渲染——**不是**收敛机制未变。**修正 §2 结论 A**：GPU 路径下 resize 能及时触发合成器提交新尺寸帧（帧提交语义与软件不同），但“合成器持续活跃”仍不成立（damage-driven 不变，静止仍 0 帧）。

### 3.2 推断 / 未验证（不得当作事实引用）

| 说法 | 状态 | 依据 |
|---|---|---|
| "GPU OSR 合成器持续活跃，从机制上消除死锁" | **错误推断，已废弃** | Chromium 合成器无论 GPU/软件均 damage-driven（§2-C）；GPU 直通只改交付（§2-A）。交接文档 §3.2 曾沿用此说，需以本文为准 |
| “GPU 路径下 resize 能及时触发合成器提交新尺寸帧” | **已实测成立**（2026-08-04） | GPU OSR 单独实现后 7 次 resize 全部瞬时收敛、尾随重发 0 触发（《实施计划-GPU-OSR纹理直通》§8）；确切机制（Invalidate 真提交 vs viz 自动重分配）未深挖 |
| "external begin frame 在 GPU 路径有效" | **未验证** | 软件路径失效（§3.1-2）；GPU 路径是 cefclient 的标准组合（GPU + external BF），但本工程未测 |

---

## 4. 解决路径（按确定性排序）

目标是：**resize 后合成器必然及时产帧（即时收敛），静止时零成本（惰性）**。三条路：

| 路径 | 机制 | 确定性 | 成本 | 备注 |
|---|---|---|---|---|
| **① fork CEF 改 hold/产帧逻辑** | CEF 内部修 `ResizeRootLayer`（hold 期间也同步 compositor size）或 `WasResized`（resize 后强制合成器提交） | **最确定**——不依赖交付方式（A）与帧源（B），软件/GPU 都根治 | 小改动（一处逻辑）+ fork 维护成本 | 直达根因（CEF 自身缺陷） |
| **② GPU OSR + external begin frame 组合** | `shared_texture_enabled=1` + `external_begin_frame_enabled=1` + 宿主持续 `SendExternalBeginFrame`——cefclient 标准组合 | **待验证**（半天 spike） | 小改动 + 已有 GPU 实现 | 若 GPU 合成器正确处理外部帧（§3.2-3），可实现"即时 + 惰性？（external 是持续驱动，静止也产帧——"惰性"打折，但 GPU 直通下开销可控）" |
| **③ GPU OSR 单独（已实现）** | 只改交付（A）；但实测 GPU 路径下 resize 能及时触发合成器提交（帧提交语义与软件不同，§3.1-4） | **已实测解决死锁**（mac：7 次 resize 全即时收敛、尾随重发 0 触发） | 已实现 | 附加价值：零 CPU 读回；拖动中视觉（1:1 裁剪）不变是显示策略所致 |

---

## 5. 验证进度（2026-08-04 更新）

**路径③（GPU OSR 单独）已实测有效**（mac）：resize 即时收敛、尾随重发 0 触发（《实施计划-GPU-OSR纹理直通》§8）。**验证顺序中后续项当前不需要**：

- ~~步骤 1（加日志验证 GPU 路径 Invalidate）~~——已完成，有效（即时收敛）。
- 步骤 2（组合 ② external begin frame）——**无需再做**：路径③已解决死锁；external BF 仅在需要“强制持续产帧”（如页面动画在 GPU 路径异常）时再评估。
- 步骤 3（fork CEF ①）——**暂缓**：路径③已根治，fork CEF 作为 Win 路径落地后仍异常时的兜底。

**决策点**：若 2 有效但"持续驱动"的 CPU 开销不可接受，可折中"按需驱动"（resize 后驱动 N 帧 + 输入/动画事件驱动），此时路径②的"惰性"也成立。

---

## 6. 与现有文档的关系

- **修正**：《实施计划-GPU-OSR纹理直通-根治resize死锁.md》§2.3/§3.2 中"GPU 路径合成器持续活跃，从机制上消除死锁"的说法——**以本文 §3.2 为准**（错误推断）。该文档的 Step 1-4 实施步骤本身不受影响（CEF 侧 + Godot 侧工程不变），但**验收标准 2"resize 瞬时收敛"的达成依赖路径②或①**，非 GPU OSR 单独。
- **沿用**：《实施记录-WebDock-resize变形黑边与收敛死锁根因分析.md》§2.3（死锁链）与 §6（遗留）——其中"GPU OSR 为根治候选①"需按本文调整为"fork CEF 为候选①，GPU OSR 降为候选②（性能向）"。

---

## 7. 一句话总结

**GPU OSR 解决"像素送得快"（零 CPU 读回），external 帧源解决"催合成器产帧"（60fps 驱动），damage-driven 是合成器的固有行为（两者之下都按需）——"resize 即时收敛"需要的是"强制产帧"（① fork CEF 或 ② GPU+external 组合），GPU OSR 单独不提供它。**
