# 实施记录：WebDock 非 OSR（窗口模式）MVP 落地

> **时间**：2026-08-05（Windows 实机，RTX 4080 SUPER / D3D12 Forward+）
> **范围**：在独立分支 `feature/webview-windowed` 上**彻底删除 OSR 代码与逻辑**，切换到 CEF 窗口模式（非 OSR）——CEF 在 Godot 编辑器主窗口内创建原生子窗口直接渲染网页，像素零回传。目标：验证"拖动 resize 丝滑、无收敛死锁、无 CPU 读回"（可行性分析文档 §6.3 的 Win POC）。
> **衔接**：《技术详解-WebDock原生子窗口-非OSR可行性分析与取舍.md》（本记录的方案依据）、《技术详解-GPU-OSR与帧调度-概念澄清与解决路径.md》（OSR 概念基线，窗口模式下 A/B/C 三概念全部失效，见 §4）。
> **前置**：main 提交 2768bd24fc（渲染文档归档与修正）。

---

## 1. 结论摘要

- **窗口模式 MVP 实机验证通过**：页面 200、React UI 在 dock 区域正常渲染、resize 即时跟随（子窗口矩形随编辑器窗口缩放同步更新，无节流/尾随重发）、JS 桥 invoke 正常。
- **OSR 相关代码全部删除**：交付回调（OnPaint/OnAcceleratedPaint）、输入转发、IME 管道、节流/尾随重发/1:1 裁剪、GPU 直通判定——共 6 文件。
- **结构性收益**（源码级）：resize 走 `MoveWindow → WM_SIZE → Chromium 原生重排`，无 OSR hold 机制（该机制仅存在于 `render_widget_host_view_osr.{cc,h}`，窗口模式不实例化——browser_platform_delegate_osr.cc:39-40）；帧率跟随显示 vsync；零 CPU 读回（`Intermediate D3D Window` 交换链直接呈现）。

## 2. 实施内容（按文件，行号为当前工作区）

### 2.1 `webview_core.h`（核心层 API 面）

- 删除回调：`on_paint` / `on_accelerated_paint` / `on_focus_editable_changed`（保留 `on_load_status` / `on_query` / `on_invoke_method`）。
- `create_browser` 签名：`(id, url, w, h, bool gpu_osr_enabled)` → `(id, url, w, h, void *p_parent_handle)`——父窗口句柄（Windows: HWND；mac: NSView）。
- `resize_browser` 签名：`(id, w, h)` → `(id, x, y, w, h)`——子窗口位置+尺寸（物理像素，相对父窗口客户区）。
- 删除：`send_mouse_move/click/wheel`、`send_key_event`、`set_focus`、`ime_set_composition/commit/cancel`、`MOD_*/KEY_*/MOUSE_*` 常量。
- 类注释更新为窗口模式语义（external_message_pump 对窗口模式同样适用——cef_browser.h:716-717）。

### 2.2 `webview_core.cpp`（核心层实现）

- `init`：`settings.windowless_rendering_enabled = 0`（原 :1125 置 1）；`external_message_pump` 保留。
- `create_browser`：删除 OSR 全套 window_info 设置（`windowless_rendering_enabled=1`、mac `shared_texture_enabled` 分支、`external_begin_frame_enabled=0`、`windowless_frame_rate=60`、`WEBVIEW_OSR_SOFTWARE` 环境变量、`p_gpu_osr_enabled`），改为：
  ```cpp
  window_info.SetAsChild(reinterpret_cast<CefWindowHandle>(p_parent_handle),
                         CefRect(0, 0, w, h));
  ```
  父句柄为空显式报错返回（严禁静默回退）。
- `resize_browser`：删除 `WasResized() + Invalidate(PET_VIEW)`（OSR hold 收敛通道），改为 `MoveWindow(GetHost()->GetWindowHandle(), x, y, w, h)`（Windows）；mac 分支暂为空操作（依赖 CEF 子 view autoresizing，未实机验证，注释标注）。
- `ClientDelegate`：删除 `width_/height_` 成员与 `set_size`、`getScreenInfo/getViewRect/onPaint/onAcceleratedPaint` 改空实现（wrapper 纯虚接口约束，窗口模式下 CEF 不调用——cef_render_handler.h:140-141）；`focusedEditableNodeChanged` 改空实现（IME 原生处理）。
- 删除：`handle_paint`（BGRA→RGBA 交换）、`handle_accelerated_paint`、`Impl::paint_buffer`、`BrowserEntry.width/height/focus_on_editable`、输入/IME 全部实现（含补充平面字符代理对拆分逻辑）。
- `pump()` 注释更新（窗口模式每帧泵送语义不变，代码不变）。

### 2.3 `webview_manager.h/.cpp`（桥接层）

- 删除：`gpu_osr_capable()`（mac Metal 判定）、`send_mouse_*`/`send_key_event`/`set_focus`/`ime_*` 转发、`_on_paint`/`_on_accelerated_paint`/`_on_focus_editable_changed` 静态分发、`init_core` 中对应回调接线。
- `create_browser`/`resize_browser` 签名同步（透传父句柄 / x,y）。

### 2.4 `web_panel.h/.cpp`（面板层，整链重写）

- 删除：`texture`/`paint_image`/`paint_buffer`/`gpu_texture_rid`/`gpu_texture`/`gpu_path_active` 等纹理状态；`set_paint`/`set_accelerated_paint`/`_free_gpu_texture`/`set_focus_editable`/`_set_ime_active`/`_gui_input`/`_get_modifiers`/`_key_to_windows_vk`；`NOTIFICATION_DRAW` 纹理绘制（1:1 裁剪/露底色策略）；25ms 节流 + 250ms 尾随重发（`RESIZE_THROTTLE_MS`/`pending_size_`/`applied_size_`/`last_paint_size_`）；IME 状态机（`ime_composing` 等）与焦点/IME 相关通知。
- 新增 `_sync_native_bounds()`：每帧（`NOTIFICATION_PROCESS`）计算 `get_global_rect() × window_get_scale(win_id)` 得物理矩形，与上次不同才 `resize_browser(x, y, w, h)`。
- `sync_size()`：首次创建时取父窗口原生句柄（Windows `DisplayServerEnums::WINDOW_HANDLE` / mac `WINDOW_VIEW`）并创建浏览器；已创建后矩形同步走 PROCESS。

### 2.5 未改动

- `editor_web_dock.cpp`（仅用 set_url/connect，兼容）、`web_bridge`（协议层，与渲染模式无关）、pump、JS 桥、缓存槽位锁、mac runtime 解析。

## 3. 构建

- 命令：`python misc/scripts/build.py --preset dev --jobs 16`。
- 结果：编译通过并链接成功；仅 1 条既有警告（`webview_core.cpp(278) C4530`，C++ 异常未启用异常展开——本 TU 一直存在，非本次引入）。
- 构建途中 3 处编译错误已修复：
  1. `Impl::paint_buffer` 在 shutdown 的残留引用（删除遗漏）——补删；
  2. `DisplayServer::WINDOW_HANDLE` 应为 `DisplayServerEnums::WINDOW_HANDLE`（枚举在 DisplayServerEnums 命名空间）；
  3. manager.cpp 一处编辑合并损坏（emit_event 身体丢失/注释错位）——读取现场后手工修复。
- 另：`nohup ... &` 后台启动时 bash 包装进程与编辑器进程 PID 混淆，枚举窗口需按真实 PID（tasklist 1.2GB 进程）过滤。

## 4. 验证（Windows 编辑器实机）

运行：`bin/godot.windows.editor.dev.x86_64.console.exe -e --path D:/misutime/104_game/hades`。

**注意：必须带 `-e`**——不带时是运行游戏而非编辑器：EDITOR 级模块初始化在 TOOLS 构建下无条件执行（main.cpp:3718），而 EditorNode 仅在 `-e` 时创建（main.cpp:4506），deferred 注册会报既有的 "EditorNode not ready"（base 分支同样存在，与本次改动无关）。

| 验收项 | 结果 | 证据 |
|---|---|---|
| CEF 初始化 | ✅ | `[webview_core] init: CEF initialized` |
| 浏览器创建（SetAsChild 生效） | ✅ | `[WebView] WebPanel browser created: id=0`（无 "parent handle required" 报错） |
| WebDock 注册 | ✅ | `[WebView] WebDock registered (LEFT_UL)` |
| 页面加载 | ✅ | `page loaded: .../index.html (status 200)` |
| JS 桥 invoke | ✅ | `invoke: id=0 method=scene.get_node_count` / `editor.get_ui_font_size` 等 |
| 子窗口存在且可见 | ✅ | 窗口枚举：编辑器 `Engine` 窗口 (1933x1045) 内 `Chrome_WidgetWin_1` (10,102 342x921, vis=True) + `Chrome_RenderWidgetHostHWND` + `Intermediate D3D Window`（CEF 自绘 D3D 交换链，零读回） |
| 网页显示 | ✅ | 屏幕截图确认 WebDock React UI 完整渲染（"已连接"/场景节点数/刷新/创建节点/撤销/重做/选中节点区；右侧 3D 视口 hero 模型） |
| resize 即时跟随 | ✅ | 编辑器窗口 1037x636 → 1400x900（SW_RESTORE + SetWindowPos），子窗口 (682,433 160x511) → (117,208 312x776) 即时同步——MoveWindow 每帧同步路径生效，无收敛窗口 |
| 输入/IME | ⚠️ 结构性生效 | 子窗口为真实 HWND，原生接收鼠标/键盘/IME（Windows hit-testing），未做专项自动化 |
| 进程树 | ✅ | 1 编辑器 + 5 CEF helper（renderer/gpu/network/storage 等）；退出后 0 残留 |

**语义验证**：resize 路径已无 OSR hold 机制——`WasResized`/`Invalidate`/节流/尾随重发代码全部删除；拖动分隔条时内容按新宽度实时重排（Chromium WM_SIZE 原生行为），非旧纹理裁剪。

## 5. 遗留 / 待办

1. **mac 分支未实机验证**：`resize_browser` mac 分支为空操作（依赖 CEF 子 view autoresizing），`WINDOW_VIEW` 句柄路径未验证——mac 后置（可行性文档 §6.2）。
2. **输入/IME 专项验证**：子窗口原生接收，但编辑器↔网页焦点往返（点进网页后快捷键回编辑器）、中文输入法实测未做。
3. **帧率量化**：目视流畅为验收；可选测拖动中 MoveWindow 调用与 Chromium 呈现帧时间戳（结构性已无卡点，量化供记录）。
4. **多 dock 布局同步**：MVP 单面板；5~10 面板时每帧 O(N) `get_global_rect` + MoveWindow 同步未实测。
5. **编辑器覆盖物**：子窗口盖洞（Godot 画不到其上）——当前无需求，有需求时评估 `ShowWindow(SW_HIDE)` 方案。
6. **文档联动**：本分支代码与《技术详解-CEF-OSR渲染机制与像素链路》《技术详解-GPU-OSR与帧调度》的"当前工程"引用已过时（OSR 代码已删）——待窗口模式稳定后统一更新或标注。

## 6. 关键代码位置（窗口模式核心路径）

- 窗口创建：`modules/webview/webview_core.cpp` `create_browser`（`SetAsChild`）
- 窗口移动/显隐：`modules/webview/webview_core.cpp` `resize_browser`（`MoveWindow`）/ `set_browser_visible`（`ShowWindow`）
- 矩形与可见性同步：`modules/webview/web_panel.cpp` `_sync_native_bounds`（NOTIFICATION_PROCESS 每帧）
- 父句柄获取：`modules/webview/web_panel.cpp` `sync_size`（`window_get_native_handle`）

## 7. 代码审查修复轮（2026-08-05，2 个 reviewer 并行）

两个 reviewer（核心层 / 面板层）均 request-changes，共 6 个 finding + 1 质量项，**全部采纳修复**：

| # | 严重度 | finding | 修复 |
|---|---|---|---|
| 1 | P1 | core.cpp `resize_browser` mac 分支丢弃 x/y/w/h 却返回 0（谎报成功） | 改为 log_stderr + 返回 -1 显式失败（mac 后置实现前不静默） |
| 2 | P2 | `MoveWindow` 失败/`GetWindowHandle()` 空仍返回 0（静默） | 记录含 GetLastError 的日志 + 返回 -1 |
| 3 | P3 | `pump_requested` 原子标志已无门控作用（OSR 节流残留） | 删除字段/store/exchange 及 `<atomic>` include；onScheduleMessageLoopWork 留空实现 |
| 4 | P1 | 面板隐藏（dock 折叠/切页）时子窗口仍盖住其他内容（Godot 可见性不传播到 OS 子窗口） | 新增 `set_browser_visible`（Win `ShowWindow`），`_sync_native_bounds` 每帧比对 `is_visible_in_tree()` 同步显隐，重显时强制重下发矩形 |
| 5 | P2 | 浏览器重建后 `last_phys_rect_` 缓存未失效 → 新 HWND 停在 (0,0) | EXIT_TREE 与创建成功路径复位 `last_phys_rect_`（哨兵保证新浏览器至少完整下发一次） |
| 6 | P2 | `get_global_rect` 不含 Viewport canvas/CanvasLayer transform，层级未约束 | 类注释 + 同步处注释显式约束仅支持主窗口根视口；sync_size 对嵌入视口（`get_parent_viewport() != nullptr`）WARN_PRINT |
| 7 | 质量 | manager.h 残留 on_paint 回调注释 | 更新为 on_load_status / on_query / on_invoke_method |

**修复后回归**（Windows 实机）：编译链接通过（仅既有 C4530 警告）；页面 200、浏览器创建、dock 注册正常；编辑器 1500x950 缩放后子窗口 (682,433 160x511)→(117,208 262x826) 即时跟随；ShowWindow 显隐原语验证通过。

## 8. 实测 bug 修复：移动 dock 后 WebDock 空白（2026-08-05）

**现象**（用户实测）：打开编辑器 WebDock 正常显示；把 WebDock 移动到右侧 dock 或与其他原生 dock 合并后，内部空白（无网页）。拖拽尺寸正常。

**根因**（代码 + 源码双重确认）：
- dock 移动 = `EditorDockManager::_move_dock` 的 `parent->remove_child(p_dock)` + `p_target->add_child(p_dock)`（editor/docks/editor_dock.cpp:360/:385）——WebPanel 出树再入树，触发 `NOTIFICATION_EXIT_TREE` / `NOTIFICATION_ENTER_TREE`。
- 面板的注册 + 浏览器创建逻辑原本挂在 `NOTIFICATION_READY`——**`_ready()` 仅首次入树触发一次**，重新入树不重触发。EXIT_TREE 已 `destroy_browser` + `unregister_panel`（browser_id=-1），重新入树后不再注册/创建 → 子窗口已销毁 → 空白。
- 拖拽尺寸正常 = 移动前的 LEFT_UL 场景；移动后的空白与尺寸逻辑无关。

**修复**（web_panel.cpp）：注册 + 创建从 `NOTIFICATION_READY` 移到 `NOTIFICATION_ENTER_TREE`（每次入树触发）；EXIT_TREE 的销毁/注销/缓存复位不变。入树时机安全：`get_window()`/尺寸在 ENTER_TREE 可用（不可用时 sync_size 判空返回，RESIZED 补发）。

**验证**：修复后启动回归通过（浏览器创建/页面 200/dock 注册正常）；dock 移动场景需用户实机复测（自动化无法驱动编辑器内 dock 拖拽）。

**备注**：dock 关闭（`_close_dock` 移到 closed 父 + hide）与重新打开同样走 remove/add —— 修复后关闭时子窗口经可见性同步隐藏、重开时重建并显示，语义一致。

## 9. 实测 bug 修复：非 OSR 后网页放大 2 倍（DPI 错配，2026-08-05）

**现象**（用户实测）：非 OSR 改版后网页内部文字比 Godot 原生界面大约一倍多（~2 倍）；OSR 时代字体大小与原生一致。

**根因**（实测数据链）：
- 系统 200% 缩放（192dpi）同时作用于两者，但**响应方式不同**：
  - CEF 子窗口：per-monitor DPI aware，200% 正确生效（`devicePixelRatio=2`，64 CSS px 参照块 = 128 物理 px 实测）；
  - Godot 编辑器：`window_get_scale=1.0`（临时日志实测）——内容按逻辑=物理渲染，200% 未放大物理像素（EDSCALE 逻辑缩放，[editor-font] 实际字号 24px）；
  - 错配结果：网页 24 CSS px → 48 物理 px，原生 24 逻辑 px → 24 物理 px → **网页 = 原生 2 倍**。
- **OSR 时代为什么没这个问题**：`getScreenInfo` 回调硬编码 `device_scale_factor=1`（原注释 "M1b:无 DPI 缩放处理"）——CEF 强制 1:1 渲染，恰好与 Godot 的 1:1 一致。非 OSR 窗口模式 CEF 走系统 DPI，该硬编码丢失。

**修复**（webview_core.cpp `AppDelegate::onBeforeCommandLineProcessing`）：加 Chromium 开关 `--force-device-scale-factor=1`——强制 CEF device_scale_factor=1，与 OSR 时代语义一致（1 CSS px = 1 Godot 逻辑 px）。

**验证**（修复后实机）：
- 页面 `devicePixelRatio` 2→**1**，`innerWidth` 160→**320**（CSS 视口 = 面板逻辑尺寸，与 OSR 时代一致）；
- 标定：24 CSS px 块 → 24 物理 px、14 CSS px 块 → 14 物理 px（1:1）；
- 字形物理高度（Python 像素测量）：网页"已连接" 26px = 原生 dock 标题"WebDock" **26px —— 完全一致**（网页 24 CSS px × EDSCALE 对齐）；
- 原生菜单栏 12px 为 Godot 主题小字号（菜单栏非对标对象）。

**遗留**：`--force-device-scale-factor=1` 为固定值——若未来 Godot 窗口支持真 DPI 缩放（window_get_scale>1），需改为传 Godot scale（create_browser 参数化），与 OSR 时代"恒 1"的简化一致，暂不处理。
