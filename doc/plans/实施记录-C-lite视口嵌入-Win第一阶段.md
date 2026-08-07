# 实施记录：C-lite 视口嵌入（Win 第一阶段：阶段 0 + Win spike 验收）

> 日期：2026-08-07
> 范围：视口方案重抉择后的首轮实施——阶段 0（零成本前置查证）+ 阶段 1（Win spike：fork 改动 + Electron 接线 + W1-W4 验收）。
> 关联文档：`视口渲染-窗口嵌入方案-设计与验证计划.md`（方案/验证计划）；`视口渲染-2B黑屏问题-分析报告.md`（2B 归档背景）。
> 基线：dev = `041131f20c`（PR#3 electron 工程化）；2A/2B 视口实现归档于 `feature/viewport-2b-sharedtexture`。

---

## 1. 本次实施的决策基线（2026-08-07 重抉择）

- **Windows = C-lite**：Godot 视口窗口作为 Electron 主窗口的 **owned window**（复用上游 `--wid` 机制），几何同步、输入直达、零帧传输。
- **macOS = M2'（首选）**：CAContext/CALayerHost 远程 layer 托管（复用上游 `--embedded`）；**M3**（IOSurface → importSharedTexture）为备选。
- 硬约束：**不 fork Chromium/Electron 源码**；HTML 永不覆盖视口（overlay 在 Godot 侧）；C4 崩溃隔离已放宽（用户决策：同生共死可接受）。

## 2. 前置查证（fork 树内机制事实，阶段 0）

| 事实 | 位置 | 结论 |
|---|---|---|
| `--wid <hwnd>` 参数 → `init_embed_parent_window_id` → `DisplayServer::create` | main/main.cpp:1984-1992, 3311 | C-lite 挂接入口，spawn 时传 Electron 主窗口 HWND |
| Windows 主窗口以 parent 为 **owner** 创建（WS_POPUP + parent = owned，非 WS_CHILD） | display_server_windows.cpp:2585-2586, 7239-7240, 7277 | owned 语义 = 无任务栏项/随 owner 最小化/置顶 |
| **AttachThreadInput**（输入/焦点同步） | display_server_windows.cpp:7286-7289 | W3 键盘直达的基础 |
| `window_set_transient` 仅支持**进程内** WindowID 且仅 exclusive 设 `GWLP_HWNDPARENT` | display_server_windows.cpp:2407-2444 | **跨进程 owner 挂接唯一路径 = spawn 时 `--wid`**（运行时挂接不可行） |
| `is_embedded_in_editor` 影响面 | scene/main/window.cpp:1291（主窗口 resize 禁令，恰好符合需要）；其余 Wayland 专属 | Win 上 `--wid` + `--editor` 低风险 |
| 官方 spawn 契约：`--wid <window_get_native_handle(WINDOW_HANDLE)>` | editor/run/game_view_plugin.cpp:1363-1365 | Electron 侧照抄此模式 |
| macOS `--embedded` → CAContext + CAMetalLayer/CALayer → contextId 上报 | main/main.cpp:1441-1447；display_server_macos_embedded.mm:216-221 | M2' 游戏侧机制现成 |
| 宿主侧 CALayerHost 创建 | display_server_macos.mm:3213-3217 | Electron addon 需镜像（约 3 行 ObjC + 插入视图） |
| **mac 输入修正**：CAContext 远程 layer 非 NSView，**无法接收原生事件**（游戏侧经 `EmbeddedDebugger::_msg_event` 注入） | embedded_debugger.h:59-66 | M2'/M3 均需输入转发——mac 融合 UI 的输入转发是结构成本，C3 仅 Win 可满足 |
| Electron `importSharedTexture` 要求 handle 在**调用进程内有效**；IOSurfaceRef 不能按整数值跨进程传 | electron_api_shared_texture.cc:586-589, 731-735；shared_texture/README.md:15-17 | M3 需 mach_port 或全局 IOSurface 交接，成本高于直搬 2B 接线 |
| Electron 43 最低部署目标 = **macOS 13.0**（硬断言） | electron/BUILD.gn:53-56 | 技术下限；**产品决策（2026-08-07）= 仅 Apple Silicon + macOS 15+**（见计划文档 §5.5） |

## 3. 改动清单（本阶段，未提交）

### 3.1 Godot fork（4 文件）

| 文件 | 改动 |
|---|---|
| `platform/windows/display_server_windows.cpp` | 放开 embedded 窗口自移动/缩放：`window_set_position`（原 :2363-2365）、`window_set_size`（原 :2503-2505）的 `if (wd.parent_hwnd) return;` 移除，替换为 C-lite fork 注释；min/max size 限制**保留**（编辑器初始化触发一次无害 print，见 §5） |
| `modules/gd_provider/registry.cpp` | 新增 `viewport.set_window_rect {x,y,w,h}` 注册（finite 数字 schema，required 全四项） |
| `modules/gd_provider/ops.h` | 声明 `h_set_window_rect` |
| `modules/gd_provider/ops.cpp` | 实现 `h_set_window_rect`：embedded 门禁（`is_embedded_in_editor` 否则 `not_embedded` 错误）、类型/有限性校验（沿用 h_set_node_position 模式）、调 `window_set_position` + `window_set_size`（`DisplayServerEnums::MAIN_WINDOW_ID`）；新增 `servers/display/display_server.h` include |

### 3.2 web/app（7 文件）

| 文件 | 改动 |
|---|---|
| `src/shared/ipc.ts` | 新增 `ViewportRect` 接口 + `viewportRectChanged` 桥方法 + `IPC.viewportRect = "viewport:rect"` 通道 |
| `electron/preload/index.ts` | `viewportRectChanged` → `ipcRenderer.send` |
| `electron/state.ts` | 新增 `viewportRect` 缓存（DIP，相对内容区） |
| `electron/main/godot.ts` | spawn 参数：主窗口存在时加 `--wid <hwnd>`（`getNativeWindowHandle().readBigUInt64LE`），否则 `--resolution` 兜底（A 方案）；新增导出 `syncViewportRect(p_window_bounds?)`：视口矩形 DIP → 物理像素 × scaleFactor → Godot 屏幕坐标空间（单屏假设，原点 0,0）→ `client.invoke("viewport.set_window_rect")`；`contentBoundsFromWindow` 换算 will-* 事件的外框为内容区 |
| `electron/main/ipc.ts` | `ipcMain.on(IPC.viewportRect)`：sender 校验 → 缓存 → `syncViewportRect()` |
| `electron/main/index.ts` | **启动重排**：`setupIpc() → createWindow() → initGodot()`（原 initGodot 先于窗口创建，spawn 时拿不到 HWND）；窗口事件接线：`will-move`/`will-resize`（提前摆位，同帧到达）+ `move`/`resize`/`maximize`/`unmaximize`/`minimize`/`restore` 兜底 → `syncViewportRect` |
| `src/renderer/src/App.tsx` | 中栏重构：状态条（Godot 进程/能力面指示，移出视口区）+ 视口区 `<section ref>`（C-lite 占位）；`ResizeObserver` → `window.godot.viewportRectChanged`（DIP，`getBoundingClientRect`） |

## 4. 构建与验证

### 4.1 构建

- Godot：`python misc/scripts/build.py --preset dev --jobs 20 -- debug_symbols=no`——首轮 47s 通过（增量）；fork 改动后两轮：首轮编译错误（`MAIN_WINDOW_ID` 应为 `DisplayServerEnums::`，修正）→ 15s 通过。
- web/app：`pnpm --dir web/app run build`——renderer/main/preload 三环境通过。
- 冒烟：`--version` = `4.8.dev.custom_build.041131f20`；`--wid 0` → "must be different then 0"；`--embedded`（Win）→ "only supported on macOS, aborting."——解析路径均正确。

### 4.2 W1-W4 验收（实测证据）

启动：`electron.exe web/app`（cwd=仓库根）→ spawn Godot（`--path test-projects/provider --editor --wid <hwnd>`）。

| 项 | 结果 | 证据 |
|---|---|---|
| W1 嵌入挂接 | ✅ | spawn 日志 `--wid 331182`；Win32 枚举：Godot 窗口 `hwnd=0x50d68 rect=(623,285,611×601)` **owner=0x50dae（= Electron 主窗口）**；Godot 日志：WS server 就绪 → 握手成功 → **Registry 17 能力**（16 MVP + viewport.set_window_rect） |
| W2 几何同步 | ✅ | `SetWindowPos` 移动/缩放 Electron (120,80,+240,+180) → Godot 同量跟随 (120,80,240,180)，**相对偏差 (0,0,0,0)** |
| W3 输入直达 | ✅ | SendInput 点击 Godot 窗口中心 → `GetForegroundWindow() == 0x50d68`（键盘直达） |
| W4 窗口形态 | ✅ | Electron 最小化 → Godot `IsWindowVisible=False`（owned 随隐）；还原 → 恢复可见且 rect 保持 (743,365,851,781) |

### 4.3 遗留观察

- `Embedded windows can't have a minimum size.`——编辑器初始化触发 min/max policy block（保留的 2452-2454/2478-2480），无害 print_line；如需干净可后续放开。
- 当前测试项目渲染后端 = OpenGL Compatibility（M1 基线设置），与嵌入机制无关。
- **内容形态（2026-08-07 确认）**：嵌入窗口内容 = 完整原生编辑器 UI（并存期 D7 形态，M3 UI 树抑制暂缓）——C-lite 窗口机制与内容正交，机制验证不受影响；仅 3D Viewport 内容留待 M3。

### 4.4 启动期焦点死锁：发现与修复（2026-08-07）

**现象**：`--wid` 嵌入 + Godot splash 期间反复点击 Electron 窗口抢焦点 → 双方主线程消息循环冻结（WM_NULL 2s 超时）、CPU 静止，只能杀进程。

**证据链**：
- A/B：禁用 onReady 同步后仍死锁（**与己方代码无关**）；启动完成后同量点击风暴无任何问题；
- no-focus 单侧无效：仅 Godot 侧 `--embedded-no-focus`（splash 期 WM_MOUSEACTIVATE→MA_NOACTIVATE）仍死锁——主路径是"点击 owner 激活"，不在 Godot 的 MOUSEACTIVATE 分支；
- 死锁实例取证：双方主线程 Waiting、消息循环 2s 超时（WCT 不可用、NtQuery 偏移未调通，机制细节未完全坐实——属上游 embedded 机制 + 启动期焦点争夺）。

**修复（用户接受的形态：启动期应用整体加载态）**：
1. Godot fork：新增 `--embedded-no-focus`（main.cpp，嵌入式 flags 重置加 `WINDOW_FLAG_NO_FOCUS_BIT`）+ 能力 `viewport.set_no_focus {enabled}`（window_set_flag 运行时切换已内置，display_server_windows.cpp:2943-2946）；
2. Electron：窗口创建后 `win.setFocusable(false)`（点击不激活 owner）；**解除时机 = Provider `editor.ready` 事件**（认证成功时下发，晚连接补收；Electron 幂等释放 + 1s×5 退避重试，5s 定时器兜底）；
3. React：`provider !== connected` 时全窗口 loading 遮罩（不可交互）；
4. 附带修复：ResizeObserver useEffect 依赖改为 connected（遮罩切换后重新绑定 observer，几何同步恢复）。

**ready 事件实现（2026-08-07）**：ProviderServer 首帧置 `editor_ready_`（EditorNode 已构造）；`_handle_frame` 认证成功即下发 `editor.ready` 通知（晚连接补收）——精确信号替代固定延迟。注：该通知先于客户端认证完成到达，故 Electron 侧 invoke 失败退避重试。

**验证**：splash 期 60 次脚本点击风暴 → 双方消息循环均响应、几何同步生效（窗口离开 splash 尺寸）。

**遗留**：① ~~解除时机用 5s 延迟，后续换 Provider ready 事件精确化~~ → **已解决（2026-08-07）**：`editor.ready` 事件精确解除 + 1s×5 退避重试 + 5s 兜底；② ~~启动期窗口几何（W2）与 DPI 换算细节待验收~~ → **移动滞后已解决（见 §4.5）**；视口矩形高度异常（曾观察 1228）待 W2 布局/DPI 复核；③ 根因机制细节（双线程具体等待对象）未完全坐实，如复发可尝试 minidump 取证。

### 4.5 拖拽跟随延迟：owner 跟随（2026-08-07）

**现象**：移动 Electron 窗口时 Godot 窗口跟随有明显延迟（肉眼可辨的脱离感）。

**根因**：WS 几何同步的两次事件循环跳转（Electron 'move' 事件派发 + Godot 帧轮询）+ 连续事件流下请求排队积压——实测 750px/s 连续拖动相位滞后 318px/~424ms；且排队中的陈旧绝对坐标与跟随互相覆盖。

**修复**：
1. Godot fork：`DisplayServerWindows` 每帧 `_update_embedded_follow()`（process_events 内）——按 **owner 窗口位置 + offset** 重组自身位置（offset 由 `window_set_position` 绝对定位刷新，防初始摆放/布局纠正与跟随打架）；位移路径完全无需 WS；
2. Electron：移除 'move'/'will-move' 几何同步（位移由 Godot 原生跟随）；resize/布局仍走 WS 绝对纠正。

**验证**：连续拖动 750px/s 相位滞后 **318px → 全贴合（≤2ms 采样分辨率）**；初始摆放偏移正确（offset 基线竞态已修）；纯 resize 尺寸跟随且静止后收敛到正确偏移。

**补充根因（2026-08-07 用户实测复现后追查）**：拖动期间仍有明显滞后且与焦点状态相关——真根因 = **编辑器失焦低功耗节流**（editor/editor_node.cpp NOTIFICATION_APPLICATION_FOCUS_OUT：`unfocused_low_processor_mode_sleep_usec` 默认 100ms → 10fps）。拖动 Electron 窗口 → Godot 失焦 → 主循环 10fps → 每帧跟随/轮询被拉长（失焦单跳实测 101ms）。修复：嵌入模式下跳过失焦节流（`!Engine::is_embedded_in_editor()` 门控）——失焦单跳恢复 7ms。**注：此前的"时好时坏"不是代码回退，是 Godot 窗口聚焦状态决定帧率。**

**架构定案（消除陈旧坐标污染）**：位置由 owner 跟随独占——`set_window_rect` 嵌入模式仅应用尺寸；偏移经新能力 `viewport.set_viewport_offset`（renderer 布局数据，天然新鲜）维护；offset 由 `window_set_embedded_offset` 写入并即时应用。

**遗留**：组合 move+resize 风暴期 ~8px 瞬态漂移（静止收敛，W2 细节）；视口矩形高度异常（曾观察 1228）待 W2 布局/DPI 复核。
- W6（DPI/多屏）：当前单屏 scale=1 通过；需多显示器环境验证坐标换算。
- W7（Play 窗口）：依赖 `run.*` 能力面（M2 里程碑），机制同路径（游戏进程 `--wid`）。

## 5. Git 操作记录（同会话）

- 2B 实现归档提交 `5535ebd2f9`（30 文件，+1202/-106；含 2B 交接/黑屏报告/d3d11read.cpp）；分支 `feature/viewport-2b-sharedtexture` = `8352a2e74c`(2B 阶段0+1) → `f4fcd08932`(2A) → `5535ebd2f9`(2B)。
- dev 重置至 `e9ebed34fe`，随后对齐 fetch 到的 `041131f20c`（PR#3，10 提交快进）；`git push origin dev`（origin/dev == origin/master == 041131f20c）。
- 注：`tools/d3d11read/` 构建产物（exe/obj/.i）在分支切换后从工作区消失（机制未明）；源码 `d3d11read.cpp` 安全保存在归档分支。

## 6. 待办

- [ ] 本阶段改动 review 后提交（fork 4 文件 + web 7 文件 + 本文档均未提交）
- [ ] W6 多屏/DPI 验收（需多显示器环境）
- [ ] W7 Play 窗口验收（随 M2 `run.*` 能力面）
- [ ] 阶段 2：按计划文档 §5.5 实施方案执行——Spike A（CALayerHost 插入 Electron 窗口，需 **Apple Silicon + macOS 15+** 的 Mac）→ 不通过则 Spike B（Metal→IOSurface→importSharedTexture）
- [ ] M3 前置（暂缓，用户确认 2026-08-07）：EditorNode UI 树抑制——嵌入窗口内容由"完整编辑器"切换为"仅 3D Viewport"
