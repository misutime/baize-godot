# 调研：Godot 从"自持窗口+场景树"改为"纯可替换渲染后端（外部 .NET 建窗）"的成本与边界

> 调研员报告 · 只读源码调研，未做任何代码改动 · 调研对象：`vendor/godot`（UniGo fork，分支 feature/window-config，4.8-dev）· 部分行号依 fork 现状，与上游略有出入
>
> 核心结论先行：
> - **"外部 HWND"已是成熟、窄入口的上游能力**（`--wid` → 主窗作为 owned/embedded popup 建在外部 HWND 内），不是"子窗化"。当前 UniGo 已实际把它当作"嵌入宿主"在用。从"Godot 建窗"到"Godot 渲染进 .NET 建的窗"，**主要是宿主（C#/启动侧）改调用方式 + fork 增加少量 API**，而非重做。
> - **RS 已是真正"无窗口"的服务器**（viewport 不 attach to screen 即纯离屏渲染），本 fork 的 unigo_render 走的就是这条路。要"完全无窗口 + 外部收图"，只需把 render target 导出/回读路径补上，属增量。
> - **真正的硬骨头不在渲染，而在：(1) 平台窗口过程/输入与场景树 Window/Viewport 的深度耦合**（跨进程 embedding 已经解决大半，但"主窗即外部 HWND"这条最短路径不解决"输入归谁收"的职责划分）；(2) headless 下没有 swapchain 呈现，外部呈现要么走"一窗 + vsync 换帧"，要么需要新的 C ABI 呈现协议。mobile 触摸/多窗口输入要走"纯 RS 库"几乎要新写输入桥。
> - 上游从 4.2 起 RenderingServer 就是独立于 DisplayServer 的抽象；**"纯 RenderingServer 库"最大的隐含成本是 core（OS/ProjectSettings/Thread/资源）与 scene（Window/Viewport 数据结构）的连带**——因为场景树里渲染的"单位"是 Viewport，而 Viewport 属于 scene 层。文档最下方给了"剥难度地图"。

---

## 1. 外部窗渲染的成本（`--wid` / parent_hwnd 机制）

### 结论：不是"只能 Godot 建的窗变子窗"。上游已支持"外部 HWND 作为嵌入目标"，且这是为编辑器嵌入运行时/子进程设计的一等路径。

证据链（`platform/windows/display_server_windows.cpp`，下称 dsw.cpp）：

- 命令行 `--wid <window_id>` 解析（`main/main.cpp:2083-2098`）：置 `_embedded_in_editor`、`Engine::set_embedded_in_editor(true)`；
- 构造函数把 `p_parent_window` 转成 `parent_hwnd`（`dsw.cpp:8129-8133`）；
- `DisplayServer::create(..., init_embed_parent_window_id, ...)` 全程传递（`main/main.cpp:3459`、`3473`）；有父窗时强制 `WINDOW_MODE_WINDOWED + WINDOW_FLAG_BORDERLESS`（`main/main.cpp:3381-3385`）；
- 主窗也走 `_create_window(MAIN_WINDOW_ID, ..., parent_hwnd, ...)`（`dsw.cpp:8200-8206`）；
- `_create_window` 把外部 hwnd 当作 **owner**：`owner_hwnd = p_parent_hwnd`（`dsw.cpp:7356-7358`）→ `CreateWindowExW(..., owner_hwnd, ...)`（`dsw.cpp:7374-7382`）；成功后 `wd.parent_hwnd = p_parent_hwnd`（`dsw.cpp:7395`）；
- 样式上嵌入窗口走 **`WS_POPUP` 且不带 `WS_EX_APPWINDOW`**（不占任务栏/Alt-Tab）：`_get_window_style`（`dsw.cpp:2672-2747`，`p_embed_child` 分支），WS_MOUSEACTIVATE 特判（`dsw.cpp:5791-5801`）；
- 运行时父窗关闭自毁逻辑（WM_WINDOWPOSCHANGED，`dsw.cpp:6838-6847`）；
- 嵌入窗口的尺寸/位置 API 大多被禁（"Embedded window can't…"：`dsw.cpp:2382、2468、2557、2583、2609、2807、2995、3013、3075、5325、5358`）——**这说明它是为"编辑器里跑游戏"设计的，天然禁用上帝视角下的普通窗口操作**，但正是这一点对"外部 .NET 建窗，Godot 只渲染"是有利的（宿主拥有窗口语义）。

### "外部 HWND → Godot 渲染到它"需要改哪些层（外科手术清单，按最短路径）

关键事实：**渲染设备只认 HWND**。Vulkan 平台 surface 由 `rendering_context_driver_vulkan_windows.cpp` 的 `surface_create`（读 `WindowPlatformData.window`=HWND 建 `vkCreateWin32SurfaceKHR`）创建，而 `_create_rendering_context_window`（`dsw.cpp:7584-7617`）在窗口建好后用 `wd.hWnd` 调 `rendering_context->window_create(p_window_id, &wpd)`——只要 `windows[MAIN_WINDOW_ID].hWnd` 是外部 HWND，整个渲染链无需任何改动（当前 `--wid` 嵌入跑通的就是这条链）。

所以"外部 .NET 建 HWND、再让 Godot 渲染进去"的**最小改动其实不在渲染层，而在宿主集成层**：

1. **（现成）** 启动时经 argv 注入 `--wid <hwnd>` + `--position/--resolution`，DS/swapchain 自动跟随外部窗；父窗由 .NET 创建、Godot 用 OS-owned popup 盖上。**注意**：Windows 下被 owned 的是"Godot 的顶层窗"而非"字面 client 区域子窗口"——若宿主想要的不是"盖在宿主之上"而是"嵌入宿主客户区且宿主完全控制其输入"，需要转用真正子窗语义（见清单 3）。UniGo 现状（README/文档：EditorNative 用 `editor_native_query_engine_hwnd`）走的就是 `--wid` 这条路。
2. **回读宿主已建 HWND 而非 argv**：给 unigo C ABI 加 `unigo_engine_set_parent_hwnd`/create 参数传 HWND，fork 只需在 unigo module 内部构造 argv（已有 argv 注入基建：`modules/unigo/unigo.cpp` create 拼 argv）+ 必要时直接给 DS 提供该值（`display_server_windows.cpp` 构造参数 `p_parent_window`）。宿主仍需自行把该 HWND 交给 Godot **且保证消息循环泵**（见 3）。
3. **若"外部窗"指"真正可被宿主嵌入/叠加/接收系统消息的子窗"**：需要把 `_create_window` 的 owner 语义改成**真 child**（`WS_CHILD` + `SetParent` + 尺寸锁定到父客户区），并补"父窗尺寸变化 → 同步子窗/swapchain"回调——这是**上游没有做过的**（上游 embedding 是给编辑器嵌游戏进程用的，编辑器那边的事件由 Godot 自己泵）。属于平台层小手术（一个 DisplayServer 方法 + WndProc 分支），预估在 dsw.cpp 一处 20~60 行量级。
4. **输入**：若采用清单 3 的真子窗，Godot 的消息泵（见 §3）可继续工作；若宿主自己想收输入，则要处理"输入归谁"的握手（谁 pump WM_ 消息、谁做 TranslateMessage/DispatchMessage——`WndProc` 与 `process_events` 目前假设 Godot 泵自己的队列，`dsw.cpp:4525-4603`）。
5. **多窗口**：DS 的 sub-window 机制（`create_sub_window`，`dsw.cpp:1944-2033` + 渲染上下文 window 自动跟随 `dsw.cpp:1957-1964`）天然支持"一主窗 + N 渲染窗"；`.NET 多视图 → N 个外部 HWND`可映射为 N 个 DS 窗（需把每个窗口的 HWND 注入点做进 create_sub_window 或子窗 attach 接口）。

> 诚实标注：`--wid` 路径是"owned 顶层窗"，与编辑器嵌游戏**完全等价**（引擎内部即如此用）。真正的"外部原生控件子窗 + 双向消息路由"是上游空白，需要 fork 新增并维护，是清单里唯一的"平台层新代码"。

---

## 2. RenderingServer 独立化（脱离 DisplayServer / 场景树）

### 结论：RS 在"渲染对象管理 + 离屏 render target"层面已经独立；**最小依赖闭包不小**（core + scene 的 Viewport/Window 数据结构 + OS/ProjectSettings），但"纯 RS 库"是可达成、非重构级的目标。

源码证据：

- **渲染循环不在 DS 里**：`Main::iteration` 每帧调 `RenderingServer::get_singleton()->draw(wants_present, ...)`（`main/main.cpp:5166、5170`）；`RenderingServerDefault::_draw` → `RSG::viewport->draw_viewports(p_swap_buffers)`（`servers/rendering/rendering_server_default.cpp:76-111`）；RS 可在独立渲染线程跑（`rendering_server_default.cpp:276-284、415-424`），**DS 只作为线程钩子**被调用（`release_rendering_thread`/`gl_window_make_current`）。
- **"渲染到窗口"只是 Viewport 的一个可选属性**：`viewport_attach_to_screen`（`servers/rendering/renderer_viewport.cpp:1156-1180`）把 viewport 的 `viewport_to_screen` 设为某 WindowID；不 attach 就是纯离屏 render target（texture），与任何窗口无关。整条 blit 链（`renderer_viewport.cpp:782-984`）最终只落到 `RSG::rasterizer->blit_render_targets_to_screen(window_id,...)` + `swap_buffers`。
- **RS 对 DS 的真实耦合点（很少且可旁路）**：
  - `renderer_viewport.cpp:345-348`（gl_compatibility 专用，需 `gl_window_make_current`）；
  - `renderer_viewport.cpp:376-382`（HDR 亮度查询，仅 HDR 路径）；
  - `renderer_viewport.cpp:1726-1728`（`call_set_vsync_mode` 转发到 DS，仅显示用 vsync）；
  - splash（`renderer_compositor_rd.cpp:250-260`）与 `rasterizer_dummy.cpp:47` 的 `swap_buffers`。
  这些在"离屏渲染 + 外部呈现"下要么走 dummy no-op、要么把 WindowID 换成"呈现句柄"映射即可。
- **纯 RS 的"呈现出口"已被抽象**：RenderingDevice 持 `RenderingContextDriver`（surface 建/换链，`servers/rendering/rendering_context_driver.h:46-123`），swapchain 创建只认 `WindowPlatformData{HWND,HINSTANCE}`（`platform/windows/rendering_context_driver_vulkan_windows.h`），Windows 驱动 = 薄封装。要做"外部 HWND 数组"，给 context driver 增加一个"外部窗口"注册表是干净切口。

### 依赖闭包（诚实评估，最小 = 大体上是现在的精简 build 本身）

- 静态/编译期依赖：`servers/rendering/*`（含 storage/scene/canvas/environment 全套）、driver（vulkan + 平台 context driver）、RD/glslang 编译链、`core`（math/string/variant/OS/Thread/ProjectSettings/…）、资源系统；RenderingServerDefault 内部 `RendererCompositor*` 运行时依赖 RSG 各 storage，都归 servers/rendering 自洽。
- 结构依赖：渲染单位是 **Viewport（scene 层 `scene/main/viewport.{h,cpp}`）**——render target、viewport_to_screen、canvas 变换都挂在 Viewport 上，而场景树 root 是 `Window`（`scene/main/window.cpp:2052+ SceneTree 构造、2070`），它同时是"场景树的头"和"一个带 DS window_id 的 Viewport"。**所以"绕开场景树"目前是绕开节点树逻辑，不是绕开 scene 数据结构**（Viewport/Window/World3D/Environment 仍会以"引擎对象"形态存在）。
- 单例清单（main.cpp 建立顺序，`main/main.cpp:3669-3675` audio、`3646-3650` RS、`3328` Input、`3459` DS）：**RS 构造必须晚于 DS**（构造时引用 DS 建 context window），**而 DS 的建窗与 RS 的 init 深度交错**（`dsw.cpp:8200-8210` `_create_window` 之后立刻 `_create_rendering_context_window`+`RenderingDevice::initialize`）。"纯 RS 库"的实质工作 = **打破这条初始化顺序**，让 RS init 不再假设 DS 已给 MAIN_WINDOW 建了 HWND/surface，改为显式 `RS::set_present_target(hwnd/外部句柄)`。这是初始化重构，不是 API 重写。

### 离"完全无窗口的 RS 后端"还差什么（相对本 fork 现状）

- 本 fork unigo_render 已在做"C# 命令 → RS，不经场景树节点"（`modules/unigo/unigo.cpp` 注释与实现；render_setup 拿 `SceneTree::get_singleton()->get_root()` 的 root Window viewport + `get_world_3d()` scenario）。它仍挂在**引擎主窗的 viewport 上**（`render_setup` 依赖 root viewport → attach camera），所以画面呈现依赖窗口/swapchain。
- 差距清单：
  1. RS init 从"跟随 DS 主窗"改为"可指定外部呈现目标或纯离屏"（初始化顺序改造）；
  2. viewport → 外部呈现目标的 blit/swap 协议（含 vsync、present 语义、以及 headless/RD 下无窗口的"虚拟窗口"抽象）；
  3. 画面读回（若宿主想要位图/纹理而非让 Godot 直接刷 HWND）：`render_target` 已有 texture RID（`texture_storage` 的 render_target 对象），需要 C ABI 导出（读回内存或绑定为外部纹理），本 fork 尚无；
  4. C# 侧自己管 root viewport 的 size/content scale/渲染驱动属性（目前由 `Window`/SceneTree 构造设置，`scene/main/window.cpp:1686-1720`、`scene_tree.cpp:2132-2206`）。

---

## 3. 输入旁路（窗口归 .NET 后，Godot 输入还能用吗）

### 结论：**Input 单例/InputMap 是纯 core 层，与窗口解耦，可继续用**；但"事件从哪来"是平台层职责，这是真正要重新划分的部分。绕过场景树的**注入钩子存在**：`Input::event_dispatch_function` + DS 每窗 `window_set_input_event_callback`。

源码证据：

- **Input 单例** 在 DS 之前创建（`main/main.cpp:3328`），独立于窗口；状态/动作查询（`Input::is_key_pressed` 等，本 fork 已暴露 `unigo_engine_is_key_pressed`）不依赖窗口。
- **事件注入路径 = 每窗 Callable**：DS 维护 `input_event_callback`（注册：`dsw.cpp:2229-2235` `window_set_input_event_callback`），场景树 root Window 在进入树时把 `Window::_window_input` 注册为该回调（`scene/main/window.cpp:1457-1462` `_update_window_callbacks`）。DS 的 `_dispatch_input_event`（`dsw.cpp:5489-5533`）按 `window_id` 路由到对应回调。
- **`event_dispatch_function`（core/input/input.h:108、input.cpp:1755-1757）** 是 Input 单例把事件继续往下游（场景/节点）投递的钩子；本 fork 由 DS 设为 `_dispatch_input_events`（`dsw.cpp:8503`）。**宿主若想不经场景树直接收事件**：把 `Input::event_dispatch_function` 换成自己的（C ABI 可注册回调），所有 `Input::parse_input_event` 喂进来的事件都过它——但注意它默认是"Input 解析完 → 继续往 Window/Viewport 分发"的中间站，绕过它即同时失去 GUI 命中/`_unhandled_input` 等（见下）。
- **谁收系统事件取决于谁 pump 消息**：`process_events`（`dsw.cpp:4525-4603`）假设 Godot 泵自己的线程消息队列（PeekMessage/DispatchMessage → 静态 `::WndProc` → `DisplayServerWindows::WndProc`（`dsw.cpp:5735+`，HWND→window_id 查表 5744-5757））。若外部窗口的 WM_ 消息由 .NET/WinForms/WPF 线程消费，则 Godot 的 WndProc 必须能被转发/子类化（**注意 dsw.cpp:7039-7041/5727/5737/8621 已有 `user_proc` 链**——`window_set_window_event...`外还有一个 `SetWindowLongPtr(GWLP_WNDPROC, user_proc)` 机制，说明"宿主介入窗口过程"是留了口子的）。
- **绕过场景树的分发点**：Window 收到 input_event_callback 后若 `is_inside_tree()` 则 `push_input(p_ev)`（`window.cpp:2046-2048`）→ Viewport::push_input → `_gui_input_event`/`_push_unhandled_input_internal`（`scene/main/viewport.cpp:3499-3590`）。**在 Window 层截住就完全绕开场景树**（GUI/节点分发），与 RS 路径一致。
- **mobile 触摸**：平台层（Windows）触摸经 `WM_TOUCH`→`_touch_event`/`_drag_event`（`dsw.cpp:5434-5455、6980-6982`）→ `parse_input_event` 或合成 touch 事件（Input::parse 中 mouse→touch 模拟 `input.cpp:931-975`）→ 同样走 dispatch。**触摸是"窗口消息来源"而非 RS/窗口抽象自身**，外部窗若要触摸，宿主仍要泵 WM_TOUCH（或把触摸事件直接喂给 `Input::parse_input_event`——DS 已这样做，外部直接 `Input::parse_input_event(touch_event)` 即可，`input.cpp:1623-1661`）。
- 现有 `--wid` 嵌入运行（编辑器嵌游戏）下输入本来就是"Godot 泵队列 + Input 单例分发到 root Window"，外部 .NET 宿主复用此路径**不需要新引擎输入代码**，只需宿主把消息泵好或让 Godot 泵它的线程队列。

---

## 4. 音频 / 动画

### 结论：**AudioServer 与渲染/窗口零耦合**，可作为独立子系统模块化接入；**动画是纯资源/数据层（core+scene），只依赖时间与"目标对象 API"**，与窗口无关。两者都不是障碍。

- `servers/audio/audio_server.cpp`：头文件清单不含任何 DisplayServer/窗口引用（含 include 段），`AudioServer::init/finish` 只依赖驱动 `AudioDriverManager`（`audio_server.cpp:1474-1477`），`AudioServer` 继承自 `Object`（`audio_server.h:49`），自己管理 mix 线程/锁。可独立初始化（main.cpp 顺序上它本来就在渲染之后单独 init，`main/main.cpp:3671-3675`）。**唯一注意**：AudioServer 要读取 `project.godot` 的音频配置（GLOBAL_*），即依赖 ProjectSettings——但这是"引擎配置"问题，不是窗口问题。
- 动画：`scene/resources/animation.h`（include 仅 core io/resource 等）；`AnimationPlayer` 等动画驱动目标是任意 Object 属性（经场景树时间），本 fork 文档自己已说明"动画是纯数据"。只要宿主有"每帧 tick 的时间源"（主循环已在 unigo_engine_iterate 里），动画/补间可完全在场景树/节点之外跑。

---

## 5. 已有基础：剥离程度 → "转外部窗/纯渲染库"是增量还是重做

### 结论：**是增量，不是重做**。UniGo fork 已经完成 90% 的"可嵌入纯渲染内核"基础，缺失的是"呈现目标/事件归属的外部化"这一层。

本 fork 现状（`unigo/README.md`、`unigo_modules.cfg`、`unigo_build_profile.txt`、`modules/unigo/unigo.{h,cpp}`）：

1. **模块白名单裁剪**：`modules_enabled_by_default=no` + `unigo_modules.cfg` 显式列 ~17 模块（unigo/mbedtls/zip/物理2D/3D/glslang/gltf/fbx/text_server_adv + 图像格式链）；gdscript/mono/xr/jolt 等排除（`nomono=yes`、`deprecated=no`、`d3d12=no`）。
2. **编辑器/工具链层**：`target=editor`（SConstruct 固定，非 template_release），编辑器 UI 模块不编，但 editor 目标强制依赖链已保留（zip/freetype/svg…）。
3. **纯 DLL + C ABI 外壳**：`libgodot_create_godot_instance`（Main::setup/initialize）→ `GodotInstance::start`（setup2/start）→ `unigo_engine_iterate`（Main::iteration）→ shutdown；参数/错误码全 POD；MSVC `#pragma comment(linker,"/export:...")` 强制导出（unigo.cpp）。已有 `--unigo-render-only`、`--unigo-config`、`--unigo-vsync/msaa` 等 fork 参数，宿主 argv 注入基建成熟。
4. **"不经场景树的 RS 驱动"已跑通**：`unigo_render_setup/apply` 用 RS 建 mesh/material/instance/camera/light、挂到 root viewport 的 World3D scenario（unigo.cpp）。
5. 平台侧嵌入：`editor_native_query_engine_hwnd` + `--wid` 已是当前形态（README/文档），说明 fork 早已站在这条"嵌入宿主窗口"路线上。

在此基础上"转外部窗"：
- 若"外部窗"＝ 现在 `--wid` 的 owned 嵌入 → **几乎零内核改动**（只是宿主把 HWND 换成 .NET 创建的）。
- 若"外部窗"＝ 真子窗/宿主完全控输入 → **平台层小手术**（dsw.cpp 一处 + C ABI 一个入口）。
- 若"纯渲染库，连窗口都不要" → **初始化顺序重构 + 呈现/事件两协议**，工程量在上游"display_driver 抽象"的延续上做，仍是增量（不需要删场景树/不需要写新渲染器）。

---

## 6. 诚实结论：可替换"纯渲染库"的天花板 + 剥离难度地图

### 天花板判断

- **渲染 API 层（RS + RD + 各 renderer）**：设计上就是平台无关、无窗口的（Viewport 离屏默认路径、context driver 抽象、驱动可插拔）。上帝视角的"渲染/资源/相机/灯光"命令全在 RS。
- **呈现/换帧层**：RD 的 swapchain 与平台 surface 解耦良好（RenderingContextDriver），换"呈现目标"是增接口而非改核心。
- **场景树本身**：是**可选**的消费者（本 fork unigo_render 已证明可绕开节点树逻辑），但 **Viewport/Window 数据对象是 RS 的结构依赖**（渲染单位挂在 Viewport 上、root 是 Window）。"彻底不要 scene 库"意味着要新造一套"纯 RS 场景句柄/相机"，那是把 Godot RS 当纯 GPU 库用，工程量完全不同量级。
- **平台/窗口/输入层**：Godot 的架构把"平台事件 → Input 单例 → Window/Viewport"串成闭环，**而 Viewport 又是渲染单位**——所以窗口系统与渲染系统在"Viewport 呈现"这一站是刻意耦合的。把它拆开（谁建窗、谁泵消息、谁定义"呈现帧"）正是本项目要做的剥离，属于架构性小手术而非删除。
- 真正难的不是"Godot 渲染"，而是：
  1. **事件归属/消息泵的进程与线程模型**（跨进程 embedding 简单，同进程多 UI 线程复杂；Windows 消息是线程队列）；
  2. **present/vsync 与宿主帧循环的同步**（Godot 的 draw 由 Main::iteration 驱动，宿主若自己定帧则要接管 RS::draw 的时机，或接受 RS::draw 只做离屏+由宿主读回）；
  3. **mobile**：触摸/生命周期/多指进 Input 的通道是平台窗口过程，绕开要重写事件桥（最重的一项）。

### 剥离难度地图

| 难度 | 项 | 原因（为什么） | 证据 |
|---|---|---|---|
| **易（≈已完成/纯配置）** | 模块裁剪到渲染+core | 官方 modules_enabled 开关 + 白名单已跑通 | unigo_modules.cfg / README |
| 易 | C ABI 外壳 / 生命周期 / 帧泵 | 已有 unigo_engine_* | unigo.cpp |
| 易 | 不经节点树的 RS 命令驱动 | 已有 unigo_render_setup/apply | unigo.cpp（render 段） |
| 易 | 音频 / 动画独立接入 | 与窗口零耦合 | audio_server.cpp 头/init、animation.h |
| 中 | **外部 HWND 渲染（--wid 语义，owned）** | DS/swapchain 只认 HWND；平台入口窄；fork 已用 | main.cpp:2083-2098、dsw.cpp:8129-8133、7584-7617 |
| 中 | **Input 单例 + InputMap 复用** | core 层解耦；hook 现成（event_dispatch_function / window_set_input_event_callback） | input.cpp:1755、dsw.cpp:8503、2229 |
| 中 | RS 纯离屏 + 画面读回/外部纹理 | RS 已离屏，缺 C ABI 导出与呈现协议 | renderer_viewport.cpp:1156-1180、texture_storage |
| **难** | 真子窗嵌入 + 宿主控制窗口过程/消息 | 上游无此路径；WndProc/user_proc 只到"转发层"，尺寸/输入/IME/DnD 语义要补 | dsw.cpp 5735+、6838-6847、WndProc 全段 |
| 难 | 独立初始化顺序（RS 不再先要 DS 主窗/surface） | main.cpp:3459→3646 与 dsw.cpp:8200-8210 深度交错，需拆阶段 | 同上 |
| 难 | mobile/触摸的纯外部事件桥 | 触摸通道是平台窗口过程（WM_TOUCH→_touch_event）；无窗口时需新事件注入层 | dsw.cpp:5434-5455、6980-6982 |
| **重构级** | 完全不依赖 scene 库的"纯 RS 句柄场景" | 渲染单位是 Viewport（scene 层），root 是 Window；等于新写一层场景抽象 | scene_tree.cpp:2052-2070、window.cpp 1686-1720 |

### 给决策的一句话

- 若目标是"**C# 建窗，Godot 渲染**"（Stride 同构、保留 Godot 的 Viewport/输入语义）→ **低成本**，走现有 `--wid`/外部 HWND 语义 + fork 平台小补丁，是明确的**增量**。
- 若目标是"**Godot 是纯无头 GPU 后端，所有窗口/输入/UI 归 .NET**"→ 渲染 API 白拿，但窗口→输入→Viewport 的耦合要在平台层重切一刀，**主要成本不在渲染而在输入/呈现协议**；mobile 触摸会是最痛的点。
- 诚实标注：① 以上为只读源码分析，行号为 fork 现状，**未编译验证**；② "外部窗渲染"与"无窗口离屏"是两条不同的工程路径，成本差一个量级，决策前应先确认"呈现由谁负责、输入由谁收"这两个问题；③ 上游 Godot 的 embedding（--wid）设计目标是编辑器嵌运行时进程，**跨进程**场景成熟；**同进程**（本 fork 形态）下把窗口语义完全交给宿主，是 Godot 官方从未承诺过的方向，需自行承担维护。
