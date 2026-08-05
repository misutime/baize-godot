# 验证计划：CEF GPU 加速（Win 先行，mac 后置）

> **状态**：✅ Win 实机验证完成（2026-08-03 执行）；结论见 §3.4——本验证机（虚拟显示适配器）上 GPU 路径可用但无提速，需真机重验。
>
> **定位**：CEF GPU 加速的验证计划。核心结论：**比预想简单——平台默认后端，零配置**。Windows 走 D3D
> 默认路径（C0 现已享受 GPU 加速），mac 走 Metal（固定，无选择）。本文先给 Win 验证步骤，再给 mac 后置计划。
>
> **⚠️ 路线状态（2026-08-05 批注）**：本验证在 OSR 软件读回路径（`shared_texture_enabled=0`）下完成，
> 结论仅适用于已删除的 OSR 代码。渲染已演进为**非 OSR 窗口模式**（main `f832eae09b`）——CEF 原生子窗口
> 由自身合成器随显示 vsync 呈现，无读回、无 `windowless_frame_rate` 上限，本计划不再适用；留存作为 OSR 时代 GPU 行为基线。

---

## 1. 背景与结论

| 平台 | CEF GPU 后端 | 当前状态 | 需要做的 |
|---|---|---|---|
| Windows | D3D 默认（D3D11，新版 Chromium 可能自动用 D3D12） | **已启用**（C0 全程跑在 GPU 上） | 只需验证 + 性能基线，无代码改动 |
| macOS | **固定 Metal**（唯一路径） | **已启用并验证**（2026-08-02：移除开关，GPU 进程正常、0 崩溃、页面 200） | 已完成，无需改动 |
| Vulkan | 两侧均显式 opt-in 实验路径 | 不碰 | 无 |

要点：
- **无需配置后端**：Chromium 自动选平台默认（Win D3D / mac Metal），我们零配置。
- **OSR 读回仍是 CPU**：`shared_texture_enabled=0`（OnPaint BGRA 软路径）——GPU 加速的是**合成/光栅**，
  画好后读回 CPU 像素再交给 Godot ImageTexture。提速来自光栅合成，静态 UI 效果有限，滚动/动画明显。
- **与 Godot 自身 GPU 完全独立**：CEF 的 GPU 进程用自己的 GPU context；像素以纯内存跨进程，
  无 GPU 资源共享、无联动配置（详见实施记录 §技术核心逻辑）。

## 2. 代码现状（2026-08-02，GPU 已启用后）

- `modules/webview/webview_core.cpp` mac 分支已**删除** `--disable-gpu/--disable-gpu-compositing`（C-mac-2 验证通过后移除）；仅保留 `--use-mock-keychain`（mac 专属）。
- Windows helper 单一 exe（`CefViewWing.exe`），GPU 子进程为 `CefViewWing.exe --type=gpu-process`。
- Win 命令行开关可透传：CEF 的 `GetGlobalCommandLine` 读进程 argv（mac 上 `--no-sandbox` 已验证同机制）。

## 3. Windows 验证步骤（明天执行）

### 3.1 准备
```powershell
task stage-webview    # 预构建 helper + 暂存(与 mac 相同入口,平台自动判定 windows64)
task dev              # 编引擎
```

### 3.2 验证 GPU 路径启用（3 项都过 = GPU 加速在跑）
1. **GPU 进程存在**：运行编辑器后，进程列表应有 `CefViewWing.exe --type=gpu-process ...`
   （`tasklist /FI "IMAGENAME eq CefViewWing.exe"` 或任务管理器）。
2. **无 GPU 崩溃**：日志无 `GPU process exited unexpectedly` / `FATAL: ... GPU process isn't usable`。
3. **无软件回退**：GPU 进程持续存活（若 GPU 初始化失败 Chromium 回退 SwiftShader 软件渲染——症状是
   明显慢且无 gpu-process 或 gpu-process 反复重启）。

### 3.3 A/B 性能对比（验证提速幅度）
同一场景跑两遍，对比 WebDock 页面滚动/动画流畅度：
```powershell
# A：GPU（默认，C0 现状）
& "bin\godot.windows.editor.dev.x86_64.console.exe" --path D:\misutime\104_game\refers\cef-b0-test --editor

# B：软件（临时禁用，作对照）
& "bin\godot.windows.editor.dev.x86_64.console.exe" --path D:\misutime\104_game\refers\cef-b0-test --editor --disable-gpu
```
观察点：滚动是否跟手、动画帧率、首帧出图耗时。预期 GPU 明显优于软件；若差异不大，
说明瓶颈在 OSR CPU 读回（正常，属架构固定成本）。

### 3.4 基线记录（2026-08-03 实机，本仓库 Win 验证机）

**§3.2 三项判定：✅ 全过（进程级）**

| 项 | 结果 | 证据 |
|---|---|---|
| GPU 进程存在 | ✅ | `CefViewWing.exe --type=gpu-process`（多轮运行均存在，PID 稳定） |
| 无 GPU 崩溃 | ✅ | 无 `GPU process exited unexpectedly`/FATAL；`bin/debug.log` 无新错误条目 |
| 无软件回退 | ✅ | GPU 进程加载 `d3d11.dll`+`dxgi.dll`+`nvwgf2umx.dll`（NVIDIA D3D11 驱动），无 `vk_swiftshader.dll` |

**硬件归属（2026-08-03 复核，推翻初版 WARP 误判）**：`nvidia-smi` 进程列表直接显示
`CefViewWing.exe`（GPU 进程）与 Godot 编辑器同在 **GPU 0 = NVIDIA GeForce RTX 4080 SUPER** 上；
主显示为 RTX 物理显示器（`EnumDisplayDevices`：DISPLAY1 PRIMARY，4K@160Hz，DP 主 + HDMI 副），
GameViewer Virtual Display Adapter（UU 远程串流组件，DISPLAY5-14）未连接。初版以“GPU 进程加载
WarpPal.dll、未加载 nvoglv64.dll”推断 WARP 路径为**误读**：WarpPal 是 Chromium 预加载的 fallback
（加载≠使用），nvoglv64 是 OpenGL 驱动（Chromium 走 D3D11 本就不加载）。**结论：CEF GPU 加速
确实在使用 RTX 4080 SUPER 硬件。**

**§3.3 A/B 对比（2026-08-03，最终定论）**

测量方法：临时在 `handle_paint` 加 1s 窗口帧率计数 + `pump()` 内 500ms resize 循环；页面用
纯 CSS `background-color` 动画（每帧强制重新光栅，不依赖 JS/rAF）制造持续损伤。

| 组 | 动画页 | 静态页（原始 stub） |
|---|---|---|
| A（GPU 默认） | **59~62fps 稳定**（A5/A7/F3 三轮全过） | 初始 1 帧后 0 帧（正常：无损伤不产帧） |
| B（`--disable-gpu`） | 103~117fps 稳定（B2） | 3~5fps 或 0（无损伤；离散事件低保底） |

**最终结论（推翻中间两次误判）**：
1. **GPU 加速完全正常**：CEF GPU 进程在 RTX 4080 SUPER（nvidia-smi 实证）；动画页稳定 60fps =
   `windowless_frame_rate=60` 上限（CEF 设 compositor VSync + video capturer 最小捕获周期）；
   软件模式 117fps 是无 video_consumer 捕获限速的路径特性，不是问题。
2. **0 帧/低帧 = 页面无持续损伤**：CEF OSR 只在有 damage（动画/滚动/交互/resize）时产帧，
   静态页初始 1 帧后不产帧是正常行为——不是 GPU 停摆、不是系统 idle、不是远程会话、不是
   合成器故障。**中间误判为“远程会话/系统 idle 节流”的原因：测试中途 stage_webview 把动画页
   还原成 stub，无意中换掉了变量**（A9+ 全部基于静态页数据）。
3. **宿主 external_message_pump/external_begin_frame 集成确有契约违例**（丢弃 delay、BF 错绑
   消息泵，shifu 源码级确认），且动画页下 V1（独立 60Hz BF）/V2（internal BF）变体显示正常态
   产帧不受影响——该缺陷是真实改进项但非本次 0 帧现象的原因；无动画时合成器不产帧是 CEF 设计
   行为，不依赖宿主时钟。
4. 动画页 4 轮中 A6 一轮 0 帧（其余三轮 60fps）：疑似偶发竞态，未复现，暂记为低风险待观察。

**待办（非阻塞）**：~~宿主 pump/BF 集成契约违例的修复~~ **已完成（2026-08-03）**：采用 shifu 首选方案
（`external_begin_frame_enabled=0` + 删除 `SendExternalBeginFrame`），并实测补充 pump 侧配合——
internal BF 的帧处理依赖 `CefDoMessageLoopWork` 每帧泵送（节流泵会饿死内部帧源→动画 0 帧）。
Win 实测：动画页 60fps 稳定、静态页正常、60s 无崩溃；**mac 需复验**（共享代码，机制平台无关但
未在 mac 实机验证）。cefViewQuery Win 侧通路单独排障（未开始）。

**其他基线**：页面加载 200 后立即产 1 帧首图（原 dock 尺寸 320x1846）；GPU 进程分钟级存活无重启；
长时间运行内存增长未专项测量。

**附带发现（与 GPU 无关，单独排障）**：`cefViewQuery` JS→宿主通路在 Win 多轮运行从未出现
`[WebView] query:` 日志（含 2026-08-03 冒烟），mac 文档有 query 回包证据——疑似 Win 侧 query
注入/路由未验证，需单独排查。

## 4. mac 后置计划 —— ✅ 已执行（2026-08-02 实机验证通过）

1. **代码改动**：已删除 `webview_core.cpp` mac 分支的 `--disable-gpu/--disable-gpu-compositing`（保留 `--use-mock-keychain`）。
2. **重建验证**（`task dev` 后连续 3 次运行）：
   - ✅ GPU 进程正常拉起：`CefViewWing.app --type=gpu-process`（**注意：GPU 进程跑在 base helper 上，不是 `CefViewWing (GPU).app`**——排查时勿用 `(GPU)` 后缀名找进程）
   - ✅ 无 `GPU process exited unexpectedly` / `FATAL`（3 次 0 崩溃）
   - ✅ 页面加载 200、WebDock 出图、干净退出 exit 0
   - 帧率观察：GPU 开启后 600 帧/12.5s≈48fps（软件模式 330 帧/27s≈12fps），但同配置下帧率波动大（12~48fps），不作为硬指标——以目视滚动/动画流畅度为准
3. **结论**：mac GPU（Metal）路径可用、稳定，开关移除即生效，无需回退。

> **备注（OSR 两种交付模式，未来决策参考）**：本测试为 OSR 软件读回路径（`shared_texture_enabled=0`）
> 下的 GPU 加速验证——GPU 加速合成/光栅，读回仍走 CPU；若后续要彻底免读回才考虑 `shared_texture_enabled=1`。
>
> | 模式 | 交付 | 性能 | 复杂度 |
> |---|---|---|---|
> | `shared_texture_enabled=0`（现状） | CPU 读回 → OnPaint（BGRA） | GPU 加速 + 每帧读回 CPU 瓶颈 | 低（已验证） |
> | `shared_texture_enabled=1` | GPU 纹理直通（mac: IOSurface/Metal；Win: D3D11 共享纹理） | 零读回，最快 | 高——宿主需消费 GPU 纹理，即 Metal/D3D ↔ Godot renderer 跨 API 互操作 |
>
> 两者同属 OSR，无冲突；`shared_texture_enabled` 只决定**交付方式**，不决定渲染方式。
> 纹理直通作为未来性能优化选项保留（另评），当前不实施。

## 5. 风险与注意事项

- **OSR 读回是 CPU 瓶颈**：GPU 加速 ≠ 全 GPU 管线；高分辨率下每帧读回开销固定。
  若未来要彻底提速，才考虑 `shared_texture_enabled=1`（GPU 纹理直通，需跨 API 互操作，复杂度高，另行评估）。
- **GPU 内存**：CEF GPU 进程与 Godot 各自分配显存；低显存机器（集成显卡）上同时跑可能有压力，
  开发机（独显/Apple Silicon）无碍。
- **Windows 后端自动选择**：D3D11 vs D3D12 由 Chromium 内部决定，我们**不指定**——验证时若想看后端，
  可用 `--enable-logging=stderr --v=1` 临时查日志，不必作为验收项。
- **验证基准**：本阶段只验"GPU 路径稳定 + 提速可感知"；具体 fps 指标不作为硬性验收
  （编辑器 UI 主观流畅即可）。

## 6. 交付物

- [x] §3.2 三项通过（GPU 进程存在、无崩溃、无软件回退）——2026-08-03，nvidia-smi 实证 GPU 进程在 RTX 4080 SUPER 上
- [x] §3.3 A/B 对比结论（GPU vs 软件差异）——2026-08-03：GPU 60fps（windowless_frame_rate=60 上限）vs 软件 117fps（无捕获限速）；动画页稳定产帧，静态页不产帧属正常 OSR 行为
- [x] §3.4 基线记录——2026-08-03 已写回本文档（含硬件归属实证、测试方法教训、集成契约违例待办）
- [x] mac 计划（§4）已执行并验证通过（2026-08-02）
