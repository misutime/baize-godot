# 实施记录：C++ 路线 mac 双平台适配与实机验证

> **用途**：按阶段记录 C++ 路线（CEF 151.3.12 + vendored CefViewCore）的 mac 平台适配与实机验证过程——遇到的问题、根因、解决方案、技术核心逻辑与注意事项。与《Godot编辑器UI重构方案-TS路线-CEF集成-C++路线-mac验证指南.md》配套：验证指南定义"验证什么/怎么验"，本文记录"遇到了什么/怎么修的/为什么"。
>
> 每次阶段完成追加一节，不回改历史记录；修订只以追加批注形式。
>
> **⚠️ 路线状态（2026-08-05 批注）**：本文记录 OSR 时代的 mac 适配与验证（OnPaint→ImageTexture 显示链路）。
> 渲染方案已演进为**非 OSR 窗口模式**（CEF 原生子窗口、像素零回传，main 合并 `f832eae09b`）——本文作为
> OSR 阶段历史记录保留，涉及渲染链路的"当前工程"表述不再适用于窗口模式；取舍与现状见
> `页面渲染选型-OSR与非OSR/`（《实施记录-WebDock非OSR窗口模式-MVP落地.md》《技术详解-WebDock原生子窗口-非OSR可行性分析与取舍.md》）。

---

## 阶段 C-mac-1：双平台支持与 mac 实机验证（2026-08-02）

### 阶段目标

在 Apple Silicon（arm64）上跑通 C++ 路线完整链路：`CEF 初始化 → OSR 浏览器创建 → 页面加载(200) → OnPaint→ImageTexture 显示 → 干净退出`，且 Windows C0 行为不变（双平台支持）。前置事实：Windows 侧 C0 已全部通过，但构建脚本（`cef_dist.py`/`stage_webview.py`/`SCsub`）与 `webview_core.cpp` 均为 Windows 专属——首次在 mac 上跑 `task stage-webview` 直接下载了 `_windows64` 包。

### 核心文件与功能

| 文件 | 功能 |
|---|---|
| `misc/scripts/cef_dist.py` | CEF 分发包定位/下载/解压；新增 `sdk_dir_suffix()`（宿主自动判定 windows64/macosarm64/macosx64），下载 URL 模板、SHA-256 登记表、SDK 哨兵文件均按平台分支 |
| `misc/scripts/stage_webview.py` | 预构建 + 暂存脚本；平台分支（wrapper 库名 `.lib`/`.a`、应用产物 exe/5 个 helper bundle、CEF 运行时 DLL 集/framework、cmake 参数）；mac 专属：`patch_helper_plists`（plist 占位符补全 + bundle id 统一 + entitlements 重签名）、`-DUSE_SANDBOX=ON`、构建选项指纹 |
| `modules/att_webview/SCsub` | 平台门控（windows x86_64 MSVC / macos arm64·x86_64 clang）；env_cef flags 平台分支；链接（Windows: libcef.lib+wrapper.lib；mac: wrapper.a + pthread/AppKit/Cocoa/IOSurface） |
| `modules/att_webview/webview_core.cpp` | CEF 核心层；init()/shutdown() mac 分支：`cef_load_library`、`CefMainArgs(argc,argv)`、subprocess/缓存路径、`settings.framework_dir_path`、`settings.main_bundle_path`、`--use-mock-keychain`（GPU 开关已在 C-mac-2 移除） |
| `justfile` | dev-run 平台化（`os()` 分支 exe/project 路径） |
| `modules/att_webview/ui/` | **新增**：编辑器页面源（bridge.html 桩页）移入仓库（原为项目外 `../refers/cef-smoke-test/ui`） |

### 问题与解决方案（按排查顺序，每项：现象 → 根因 → 修复 → 证据）

#### P1. mac 上下载了 Windows 的 CEF 包
- **现象**：`task stage-webview` 下载 `cef_binary_..._windows64.tar.bz2`（338MB）。
- **根因**：`cef_dist.py` 的 `SDK_DIR_SUFFIX = "windows64"` 与下载 URL 模板硬编码（`cef_dist.py` 原 :30）。
- **修复**：`sdk_dir_suffix()` 按 `platform.system()+machine()` 判定；URL/哈希/哨兵按平台；macosarm64 包 SHA-256 从官方 `index.json` 下载后实测登记（`79d59f2bbde7...3601`）。
- **证据**：`python3 misc/scripts/cef_dist.py` → `平台后缀 = macosarm64`；下载 297MB 校验通过。

#### P2. SCsub 在 clang 下编译失败（两轮）
- **现象**：`webview_core.cpp` 报 20 处 `error: cannot use 'try' with exceptions disabled`。
- **根因**：第一轮是我按文档建议给 env_cef 加了 `-fno-exceptions`（clang 下 try/catch 是**硬错误**，GCC 才是无操作）；移除后仍报错——本 fork `SConstruct:263` 把 `disable_exceptions` 默认设为 **True**，全局 CXXFLAGS 带 `-fno-exceptions`（Windows 侧 `_HAS_EXCEPTIONS=0` 时 try 为无操作所以 C0 能过）。
- **修复**：mac 分支从 env_cef 移除 `-std=gnu++17`（SConstruct:916 全局）与 `-fno-exceptions`，加 `-std=c++20 -DNOMINMAX`；NDEBUG 保留（与 Release wrapper ABI 一致，沿用 Windows 既有决策）。
- **证据**：`task dev` 构建成功（`scons: done building targets`）。

#### P3. `CefCommandLine::GetGlobalCommandLine` 段错误
- **现象**：编辑器启动 SIGSEGV，栈在 `CefCommandLine::GetGlobalCommandLine()`（`command_line_ctocpp.cc:40`）→ `WebViewCore::init`。
- **根因**：mac 上 wrapper 的 C API 经**全局函数表**分发，framework 未加载时指针为 NULL——Windows 由导入库在进程启动时自动加载 libcef.dll，mac 必须显式加载。
- **修复**：`cef_load_library("<exe_dir>/Chromium Embedded Framework.framework/Chromium Embedded Framework")` 移到 init() **最前**（任何 CEF API 调用之前），失败置终态。
- **证据**：修复后进入 `[webview_core] init: CEF initialized`。

#### P4. `icudtl.dat not found in bundle`
- **现象**：`CefInitialize` 内 ICU 加载失败（`icu_util.cc:177`）→ 进程 trap。
- **根因**：mac 的 ICU 只经 `apple::PathForFrameworkBundleResource` 找 `icudtl.dat`（在 framework bundle 的 Resources 内）；非 bundle 裸可执行文件下 CEF 的 framework bundle 定位走 main bundle 的 `Contents/Frameworks`（错误路径）。`GetFrameworkDirectory()` 优先读 `--framework-dir-path` 开关（CEF 官方机制）。
- **修复**：`settings.framework_dir_path = <exe_dir>/Chromium Embedded Framework.framework`（CefInitialize 时转成命令行开关，`util_mac::BasicStartupComplete` 据此 `SetOverrideFrameworkBundlePath`；见 `libcef/common/chrome/chrome_main_delegate_cef.cc:204,407`）。
- **证据**：helper 命令行可见 `--framework-dir-path=.../bin/Chromium Embedded Framework.framework`；ICU 错误消失。

#### P5. helper 进程 mach rendezvous 失败
- **现象**：`bootstrap_look_up ... Unknown service name (1102)` → `No rendezvous client, terminating process` → GPU/network/renderer 启动即死。
- **根因**：两个叠加——①helper `Info.plist` 的 Xcode 占位符（`$(PRODUCT_BUNDLE_IDENTIFIER)` 等）在 Unix Makefiles/Ninja 生成器下**原样保留**（只有 Xcode generator 替换），CFBundleIdentifier 变成字面量；②上游 CMakeLists 按 `CEF_HELPER_APP_SUFFIXES` 给每个 helper 配了**带后缀**的 bundle id（`com.cefview.CefViewWing.gpu` 等）。而 Chromium 的 mach rendezvous 服务名 = `BaseBundleID.MachPortRendezvousServer.<pid>`，浏览器与 helper 各用自己进程的 BaseBundleID 构造——id 不一致则 helper 连不上父进程。
- **修复**：stage 脚本 `patch_helper_plists()`：展开全部占位符（`CFBundleExecutable` 用 bundle 去 `.app` 的真实可执行名，如 `CefViewWing (Renderer)`）；**统一**所有 helper 的 bundle id 为 `com.cefview.CefViewWing`（Chromium 约定：helpers 共享主程序 id）；浏览器侧 `settings.main_bundle_path = <exe_dir>/CefViewWing.app`（`GetMainBundleID` 读该 bundle 的 id，取得同一 BaseBundleID）。
- **证据**：`plutil -p` 显示 5 个 helper id 均为 `com.cefview.CefViewWing`；rendezvous 错误消失。

#### P6. entitlements 签名（allow-jit）与 codesign 坑
- **现象**：签名后 helper 仍 SIGTRAP（后被 P7 证明是沙箱 CHECK，非签名）；另 `codesign --entitlements` 直接报 `Failed to parse entitlements: AMFIUnserializeXML: syntax error near line 6`。
- **根因**：上游 `.entitlements` 含 DOCTYPE 声明，AMFI 解析器拒绝；ad-hoc 签名（`--sign -`）默认**不含** entitlements。
- **修复**：`plistlib` 重写 entitlements 为无 DOCTYPE 标准 XML 写临时文件再签名；`codesign --force --deep --sign - --entitlements <tmp> <bundle>`。
- **证据**：`codesign -d --entitlements -` 可见 `allow-jit`/`allow-unsigned-executable-memory`/`disable-library-validation` 均为 true；`codesign --verify` 通过。另用 C 实验证明 ad-hoc+allow-jit 下 `MAP_JIT`+`vm_protect(RX)`+执行可用（排除 JIT 权限作为根因）。

#### P7. GPU/network/renderer 崩溃（本阶段最大 blocker，shifu 定位）
- **现象**：`GPU process exited unexpectedly: exit_code=5` ×3 → `Network service crashed` → `FATAL: gpu_data_manager_impl_private.cc:417 GPU process isn't usable. Goodbye.` → exit 133；renderer 崩溃报告 SIGTRAP。
- **排查弯路**：崩溃栈被符号化为 `v8_internal_simulator_ProbeMemory`（framework stripped，取最近导出符号），一度误判为 V8 sandbox/cage/MAP_JIT 问题；反汇编纠正：真实 PC 附近是 `brk #0`，且 network service（无 V8）与 renderer 同址崩溃 → 非 V8 专属。
- **根因（shifu 闭环确认）**：`content/app/content_main_runner_impl.cc:1025-1030`（Chromium 151）：
  `if (!IsUnsandboxedSandboxType(...)) CHECK(sandbox::Seatbelt::IsSandboxed());`
  我们的组合是"**启用 sandbox 但未初始化**"：helper 以 `USE_SANDBOX=OFF` 构建 → `CefWing/mac/main.mm:62-70` 的 `CefScopedSandboxContext.Initialize` 被预处理掉 → 子进程未进 Seatbelt → CHECK 失败 brk。官方样例 `tests/cefsimple/cefsimple_mac.mm:161-166` 明确：未定义 CEF_USE_SANDBOX 时必须 `settings.no_sandbox = true`（我们两样都没做）。
- **决定性反证**：同二进制仅追加 `--no-sandbox` → page loaded 200、GPU/network/renderer 全存活、exit 0。
- **修复**：stage 脚本 mac cmake 参数加 `-DUSE_SANDBOX=ON`（helpers 带 `-DCEF_USE_SANDBOX`，main.mm 走沙箱初始化）；版本标记新增**构建选项指纹**（第三行），防止改选项后静默跳过重建。
- **证据**：`flags.make` 含 `-DCEF_USE_SANDBOX`；无 `--no-sandbox` 参数下页面 200、0 崩溃、exit 0。

#### P8. 钥匙串弹窗（"godot 想访问 Chromium Safe Storage"）
- **现象**：每次启动 godot 弹钥匙串密码框；无人授权时 network service 初始化失败（与 P7 级联）。
- **根因**：NetworkService 的 OSCrypt 为 cookie 加密访问钥匙串 `Chromium Safe Storage` 项（CEF issue #2692 同款）；ad-hoc 签名每次构建/暂存 CDHash 变化 → 钥匙串 ACL（绑定应用 designated requirement）失配 → 每次弹窗。
- **修复**：`--use-mock-keychain`（mac 专属；Brave 开发构建同款做法）。编辑器 WebDock 不需要持久 cookie，mock keychain 免除弹窗（代价：加密密钥每次启动重生成，旧 cookie 不可解，对本场景无影响）。
- **证据**：framework 内存在该开关（`strings` 验证）；弹窗消除。
- **注意（2026-08-02 确认架构决策）**：网页仅是 Godot C++ 应用的**扩展 UI 层**，持久化数据一律放 C++ 侧（不依赖 webview 的 cookie/localStorage）——`--use-mock-keychain` 为**最终方案**，无需自签稳定签名身份。

#### P9. UI 源在项目外 + stage_ui 路径 bug
- **现象**：页面 404；`bin/webview/bridge.html` 在根目录而非 `bin/webview/ui/`（编辑器按 `webview/ui/bridge.html` 加载，`editor_web_dock.cpp:42`）。
- **根因**：`UI_SOURCE = ../refers/cef-smoke-test/ui`（项目外路径，产品级不允许）；`stage_ui()` 把 html 拷到 `WEBVIEW_DEST` 根（漏了 `ui/` 子目录）。
- **修复**：UI 源移入仓库 `modules/att_webview/ui/`（MVP 阶段直接收 html）；`stage_ui` 拷到 `tmp/ui/`；另修 MANIFEST 写入父目录缺失（UI 缺失时 `WEBVIEW_DEST` 不存在）。
- **证据**：`bin/webview/ui/bridge.html` 存在；页面加载 200。

### 技术核心逻辑：mac 非 bundle 嵌入 CEF 151 的 7 个要点

1. **framework 显式加载**：mac 无"导入库自动加载"——主机进程必须先 `cef_load_library`（`include/wrapper/cef_library_loader.h`），否则第一个 CEF API 调用（哪怕 `GetGlobalCommandLine`）即 NULL 崩溃；顺序必须在**任何** CEF 调用之前。
2. **CefMainArgs**：mac 构造需要真实 `argc/argv`，非 bundle 可执行文件从 `_NSGetArgc()/_NSGetArgv()`（`crt_externs.h`）取。
3. **framework_dir_path**：非 bundle 下 CEF 的 framework bundle 定位失效（main bundle = exe 文件本身）→ ICU/资源加载失败；`CefSettings.framework_dir_path` 是官方机制（转 `--framework-dir-path` 开关）。
4. **mach rendezvous 一致性**：子进程启动靠 `BaseBundleID.MachPortRendezvousServer.<pid>` 的 bootstrap 服务连接；浏览器与全部 helper 必须共享同一 bundle id（Chromium 约定），且 Info.plist 的 `$(...)` 占位符必须展开（Unix Makefiles 生成器不做）。
5. **Seatbelt sandbox 二选一**：CEF 151 mac 子进程强制 `CHECK(IsSandboxed())`——要么 `-DUSE_SANDBOX=ON`（helpers 走 `CefScopedSandboxContext` 初始化，推荐产品级），要么 `settings.no_sandbox=true`（官方 cefsimple 模式）；"启用但未初始化"是崩溃组合。
6. **helper 签名**：Apple Silicon 上 V8/GPU 需要 `allow-jit` 等 entitlements；cmake 生成的 ad-hoc 签名不含 entitlements，stage 需用仓库内 `CefViewWing.entitlements`（经 plistlib 去 DOCTYPE）重签名。
7. **钥匙串**：NetworkService 访问 `Chromium Safe Storage`；本产品（网页=扩展 UI 层，持久化在 C++ 侧）以 `--use-mock-keychain` 为**最终方案**（免弹窗、无钥匙串依赖），无需稳定签名身份。

### 注意事项（坑与预防）

- 本 fork `SConstruct:263` `disable_exceptions` 默认 **True** → 全局 `-fno-exceptions`；clang 下 `try/catch` 是硬错误（GCC 是无操作），CEF 侧 TU 需从 env_cef 移除。
- `codesign --entitlements` 拒绝含 DOCTYPE 的 plist（AMFI 解析器）；先用 plistlib 规范化。
- ad-hoc 签名 CDHash 每次构建变化 → 钥匙串 ACL 失配 → 弹窗；这是 dev 循环固有问题。
- 崩溃报告符号化对 stripped framework 不可靠（取最近导出符号，`v8_internal_simulator_ProbeMemory` 距真实 PC +11.7MB）；用 `lldb disassemble -s/-e` 按文件偏移核对。
- helper bundle 的可执行文件带功能后缀（`CefViewWing (Renderer)`），`CFBundleExecutable` 必须匹配真实名，否则 codesign/LaunchServices 认错。
- 测试运行用 `--quit-after 330`（约 30 秒）避免长时间占用屏幕。
- 改 cmake 选项（如 USE_SANDBOX）后，版本标记的构建选项指纹会触发重建；若手动清了 `bin/obj/webview/cefviewcore` 缓存则全量重建约 3 分钟。
- CEF macosarm64 官方包的 V8 为 **USE_SIMULATOR 构建**（`v8_internal_simulator_ProbeMemory` 等符号存在），软件 OSR 下页面出图需数秒~数十秒，静态页首帧后稳定。
- 本地可查源码：CEF master `/Users/misu/misutime/102_games/refers/cef`、V8 `/Users/misu/misutime/102_games/refers/v8`、上游 CefViewCore `/Users/misu/misutime/102_games/refers/CefViewCore`（排查利器）。

### 验证结果

**✅ 全链路通过（2026-08-02 实机，Apple Silicon arm64，无任何 --no-sandbox 参数）**：

| 环节 | 证据 |
|---|---|
| 构建 | `task stage-webview`（wrapper.a + 5 helper bundle + framework 暂存）、`task dev` 完整构建链接通过 |
| 生命周期 | `[webview_core] init: CEF initialized` → `WebPanel browser created: id=0` → `page loaded: ...bridge.html (status 200)` |
| 出图 | WebDock 实机目视确认页面内容（深蓝背景+文字）；诊断日志证实 paint 产帧（320x1816） |
| 干净退出 | exit 0、无 GPU/network/renderer 崩溃、无残留 helper 进程 |

### 遗留问题 / 待办

- [ ] Gate B3：`window.cefViewQuery` 双向桥往返未实测（页面桩已含 `cefQueryReady` 监听，桥代码与 Windows 共用）
- [ ] Gate B4：mac 中文输入法（IME）未验证
- [ ] Gate B5：多实例双开（root_cache_path 独立性）未实测；resize/内存增长未专项测试
- [ ] 首帧延迟：软件 OSR + simulator V8 出图慢（数秒~数十秒，静态页首帧后稳定）——**GPU 加速已启用验证（见 C-mac-2）**，首帧延迟改善待实机目视确认
- [x] 钥匙串持久化：**已决策不需要**——网页仅为扩展 UI 层，持久化数据走 C++ 侧（EditorSettings 等）；mock keychain 为最终方案（2026-08-02 用户确认）
- [ ] 平台适配改动最终合入《C++生态复核与从零选型.md》§C0.0b 构建契约（按平台分支扩展）

---

## 阶段 C-mac-2：GPU 加速启用与验证（2026-08-02）

### 阶段目标

解除 mac 的 `--disable-gpu/--disable-gpu-compositing`，验证 CEF GPU（Metal）路径在 OSR 软件读回模式下稳定可用。背景：Windows C0 一直跑在 GPU（D3D 默认）；mac 因早期 GPU 进程崩溃（实为 P7 沙箱问题）禁用了 GPU，现沙箱已修，恢复 GPU 验证。

### 变更

`modules/att_webview/webview_core.cpp` `onBeforeCommandLineProcessing`：删除 mac 分支的两行 `AppendSwitch("disable-gpu")` / `AppendSwitch("disable-gpu-compositing")`，保留 `use-mock-keychain`。

### 验证结果

**✅ 通过（连续 3 次运行，Apple Silicon arm64）**：

| 项 | 结果 |
|---|---|
| GPU 进程 | 正常拉起：`CefViewWing.app --type=gpu-process`（跑在 **base helper** 上，非 `(GPU).app`） |
| 崩溃 | 3 次均 0（无 `GPU process exited unexpectedly` / FATAL） |
| 页面 | 加载 200、WebDock 出图、exit 0 |
| 帧率 | GPU 开启 600帧/12.5s≈48fps；软件模式 330帧/27s≈12fps（同配置波动 12~48fps，不作硬指标，以目视流畅为准） |

### 关键认知

1. **GPU 进程宿主**：mac 上 GPU 进程从 **base `CefViewWing.app`** 以 `--type=gpu-process` 启动，不是 `CefViewWing (GPU).app`（后者 bundle 存在但未被选作 gpu 类型宿主）——排查时用 `pgrep -f gpu-process` 而非按 bundle 名。
2. **OSR 读回不变**：GPU 加速合成/光栅，`shared_texture_enabled=0` 读回仍走 CPU——彻底免读回需
   `shared_texture_enabled=1`（GPU 纹理直通：mac IOSurface/Metal、Win D3D11 共享纹理，宿主需跨 API
   消费 GPU 纹理，即 Metal/D3D ↔ Godot renderer 互操作；与当前 CPU 读回同为 OSR 模式，无冲突，
   作为未来性能优化选项保留，另评）。
3. **与 Godot GPU 独立**：CEF GPU 进程独立 context，无资源共享、无联动配置。

### 遗留

- [ ] 首帧延迟/滚动流畅度的 GPU vs 软件定量对比（Win 侧按《验证计划-CEF-GPU加速-Win先行.md》§3 执行，mac 目视确认）
