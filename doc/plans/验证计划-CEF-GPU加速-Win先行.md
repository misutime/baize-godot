# 验证计划：CEF GPU 加速（Win 先行，mac 后置）

> **状态**：待 Win 实机验证（2026-08-02 编写，次日执行）。
>
> **定位**：CEF GPU 加速的验证计划。核心结论：**比预想简单——平台默认后端，零配置**。Windows 走 D3D
> 默认路径（C0 现已享受 GPU 加速），mac 走 Metal（固定，无选择）。本文先给 Win 验证步骤，再给 mac 后置计划。

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

### 3.4 基线记录（写回本文档或实施记录）
- GPU 进程稳定运行时长 / 是否出现过崩溃
- 首帧出图耗时（目视即可，精确测量可临时在 `webview_core.cpp` 的 `handle_paint` 加计数日志）
- 滚动/动画主观流畅度（GPU vs 软件）
- 长时间运行（几分钟）无 GPU 进程重启、无内存持续增长

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

- [ ] §3.2 三项通过（GPU 进程存在、无崩溃、无软件回退）
- [ ] §3.3 A/B 对比结论（GPU vs 软件差异）
- [ ] §3.4 基线记录
- [x] mac 计划（§4）已执行并验证通过（2026-08-02）
