# Godot 编辑器 UI 重构（TS 路线）——C++ 路线 mac 验证指南

> **状态**：核心链路已跑通（2026-08-02 实机验证，Apple Silicon arm64）。Windows C0 已全部通过；
> mac 的 CEF 初始化→OSR 浏览器→页面 200→WebDock 显示→干净退出 全链路验证通过（日志证据见 §3）。
> 未验证项：IME 中文输入（B4）、JS 双向桥往返（B3 部分）、多实例双开（B5 部分）。
>
> **定位**：本文给同事在 mac 机器上调试 C++ 路线（CEF 151.3.12 + vendored CefViewCore）的验证点、注意事项与步骤。复用现有核心层代码，主要工作在平台适配与实机验证。

---

## 1. 目标

在 mac（优先 Apple Silicon arm64，x64 可后置）上跑通 C++ 路线的完整链路：

```text
CEF 初始化 → OSR 浏览器创建 → 页面加载(200) → OnPaint→ImageTexture 显示 → cefViewQuery 双向桥 → 干净退出
```

对应 Windows 已通过的 C0 验证集（《C++生态复核与从零选型.md》§9）。**目标不是从零移植，而是把现有跨平台代码在 mac 上验证 + 修平台差异**。

---

## 2. mac 与 Windows 的已知差异（必须处理的点）

| # | 位置 | Windows（已通） | mac（需改/验证） | mac 实际状态（2026-08-02） |
|---|---|---|---|---|
| 1 | `modules/webview/SCsub` 平台门控 | `windows + x86_64 + env.msvc` 才继续 | **需改**：门控 + arch 判断 + 编译参数（clang） | **已改**：`macos` + `arm64/x86_64` 门控；env_cef 移除 `-std=gnu++17`/`-fno-exceptions`(本 fork disable_exceptions 默认 True,clang 禁异常下 try/catch 报错,故 env_cef 保留异常)后加 `-std=c++20`；链接 `libcef_dll_wrapper.a` + CEF_STANDARD_LIBS(pthread/AppKit/Cocoa/IOSurface) |
| 2 | `misc/scripts/cef_dist.py` `SDK_DIR_SUFFIX` | `"windows64"` | **需改**：`macosx64` / `macosarm64` | **已改**：`sdk_dir_suffix()` 宿主自动判定；下载 URL/哈希/哨兵按平台；macosarm64 哈希已登记 |
| 3 | `modules/webview/webview_core.cpp` 缓存路径 | `%LOCALAPPDATA%/baize-godot/cef` | **需改**：`~/Library/Caches/baize-godot/cef` | **已改**：`$HOME/Library/Caches/baize-godot/cef`(实测生效,helper 命令行可见) |
| 4 | CefViewWing helper | `CefWing/win/main.cpp`（exe） | CefViewCore 上游已支持：`CefWing/mac/main.mm` + Info.plist + entitlements | **验证通过**：5 个 helper bundle(GPU/Plugin/Renderer/Alerts)由 cmake 构建;但 plist 的 Xcode 占位符需 stage 补齐并重签名(见 §4.9) |
| 5 | CEF SDK 包 | `cef_binary_<v>_windows64.tar.bz2` | `macosx64` / `macosarm64` | **验证通过**：macosarm64 自动下载+解压+校验(297MB) |
| 6 | Godot 构建 | `task dev`（MSVC） | Xcode + clang;`scons platform=macos` | **验证通过**：`task dev`(build.py --preset dev)完整构建+链接成功;产物 `bin/godot.macos.editor.dev.arm64` |
| 7 | IME | Windows 拼音已通 | **mac 中文输入法实机验证** | **未验证**(本机无中文输入法测试场景) |
| 8 | 渲染 | 软件 OSR 已通 | 软件 OSR 理论平台无关;GPU 路径(Metal)本阶段不验证 | **验证通过**：软件 OSR 出图(WebDock 可见);**GPU 加速已启用并验证**(2026-08-02:移除 --disable-gpu,GPU 进程正常、0 崩溃、页面 200;GPU 进程跑在 base helper 的 --type=gpu-process,非 (GPU).app) |
| 9 | DLL/搜索 | libcef.dll + exe 旁 | framework + helper bundle;`browser_subprocess_path` 路径语义不同 | **已改**：framework 与 helper bundle 随 exe 同级(bin/);`browser_subprocess_path` = `exe_dir/CefViewWing.app/Contents/MacOS/CefViewWing`;主机 `cef_load_library` 显式加载 framework |

**mac 实机新增的平台差异**(本文写作时未预见的坑,见 §4 注意事项 9-12):
- 非 bundle 裸可执行文件下,CEF 的 framework bundle / bundle id 定位失效 → 需 `framework_dir_path` + `main_bundle_path` 两个设置
- 所有 helper 必须共享同一 bundle id(Chromium 约定),且 Info.plist 占位符必须展开
- CEF 151 mac 子进程强制 Seatbelt sandbox(未初始化则 `CHECK(sandbox::Seatbelt::IsSandboxed())` brk 崩溃)→ helper 构建必须 `USE_SANDBOX=ON`
- NetworkService 访问钥匙串 "Chromium Safe Storage" → dev 构建用 `--use-mock-keychain` 免弹窗

**当前代码已预留的平台抽象**（不需重写）：
- `cef_dist.py` 的 `SDK_DIR_SUFFIX` 常量（注释写明"未来扩展 macosx64/linux64 时在此加"）
- `webview_core.cpp` 的 CEF 接线（settings/windowless/消息泵/桥）是跨平台 CEF API
- `SCsub` 门控显式报错（mac 上不会静默失败）

---

## 3. 验证点清单

> 勾选结果（2026-08-02 实机，Apple Silicon arm64，CEF 151.3.12 macosarm64）。
> 证据：构建/运行日志见各条目；关键日志行节选于 §3.1 后。

### 3.1 构建（Gate B0）——✅ 全部通过
- [x] **SCsub 门控修改后**：mac 上 scons 配置通过（不再报 windows-only 错误）
      （证据：`task dev` 完整构建成功，无平台报错）
- [x] **cef_dist.py mac 分支**：宿主自动判定 `macosarm64`；自动下载/解压/校验官方包
      （证据：`[cef-dist] 下载 ..._macosarm64.tar.bz2` + 固定 SHA-256 校验通过，297MB）
- [x] **CefViewCore 编入 SCons**：24 源文件 clang 下编译通过（C++20）
- [x] **stage 预构建**：`task stage-webview` 构建 `libcef_dll_wrapper.a` + 5 个 helper bundle 成功
      （证据：`[stage-webview] 预构建完成: .../output/Release/bin + .../libcef_dll_wrapper.a`）
- [x] **引擎链接**：`task dev` 完整构建通过，无符号/平台库错误
      （证据：`scons: done building targets` + `Linking Program bin/godot.macos.editor.dev.arm64`；唯一告警为 `-lpthread` 重复，无害）

### 3.2 生命周期与消息泵（Gate B1）——✅ 全部通过
- [x] `CefInitialize` 成功（日志 `[webview_core] init: CEF initialized`）
- [x] external message pump：每帧 `CefDoMessageLoopWork` 正常（页面成功加载+出图即泵正常）
- [x] windowless browser 创建/关闭：页面加载 200、退出后无残留 helper 进程
      （证据：`[WebView] page loaded: ...bridge.html (status 200)`；退出后 `pgrep CefViewWing` 为空）
- [x] 退出：`CefShutdown` 干净（运行结束 exit 0）

### 3.3 软件 OSR（Gate B2）——✅ 通过（resize 未专项测试）
- [x] `OnPaint` 回调产出：WebDock 面板渲染出页面内容（多次实机目视确认：深蓝背景+文字可见；
      诊断日志证实 paint 产帧 `320x1816`）
- [x] `ImageTexture` 显示：WebDock 显示正常
- [ ] resize 正常、无持续内存增长（未专项测试）
- [ ] 首帧延迟专项测量（软件渲染 + CEF macosarm64 的 V8 为 simulator 构建,页面出图需数秒~数十秒,
      静态页首帧后稳定）——GPU 加速已启用(2026-08-02 验证:0 崩溃、GPU 进程正常),首帧延迟是否显著改善待实机目视确认

### 3.4 JS 双向桥（Gate B3）——⚠️ 部分（往返未实测）
- [ ] `window.cefViewQuery({request,...})` → C++ `on_query` 收到（未实测）
- [ ] `respond_query` 应答回到 JS `onSuccess`（未实测）
- [ ] 刷新/关闭不留悬空 callback（未实测）

### 3.5 IME 中文输入（Gate B4）——❌ 未验证
- [ ] mac 中文输入法（拼音）在 `<input>` 输入正常（未验证）
- [ ] 中英文混输无吞字/重复提交/composition 残留（未验证）

### 3.6 平台特有（Gate B5）——⚠️ 部分
- [ ] 多实例/双开：root_cache_path 独立（未实测）
- [x] 旧进程占用：退出后无残留 helper 进程（证据：`pgrep -fl CefViewWing` 为空）
- [x] Apple Silicon(arm64)：CEF SDK 用 `macosarm64` 包（证据：`lipo -info` 输出 arm64；`sdk_dir_suffix()=macosarm64`）

> **关键日志（最终验证 run，无任何 --no-sandbox 参数）**：
> ```
> [webview_core] init: CEF initialized
> [WebView] CEF core initialized (C++ route).
> [WebView] WebPanel browser created: id=0 url=file:////.../bin/webview/ui/bridge.html
> [WebView] WebDock registered (LEFT_UL), url=file:////.../bin/webview/ui/bridge.html
> [WebView] page loaded: file:///.../bin/webview/ui/bridge.html (status 200)
> （0 个 GPU/network/renderer 崩溃；exit=0）
> ```
> UI 页面源已移入仓库：`modules/webview/ui/`（stage 暂存到 `bin/webview/ui/`）

---

## 4. 注意事项（坑与预防）

1. **CEF ≥120 同 root 单例**:多实例共享 `~/Library/Caches/.../cef` 会 profile 冲突——用独立 root 或验证 `OnAlreadyRunningAppRelaunch`。当前实现每实例同 root,双开必测。
2. **helper 路径必须绝对**:CEF 151 要求 `browser_subprocess_path` 绝对路径(mac 上是 helper bundle 内可执行文件路径,语义与 Windows exe 不同)。
3. **旧进程占用**:上次异常退出残留 helper 会伪装"空白/卡死"——先确认无残留再调逻辑。
4. **C++20**:CEF 151 头文件要求 C++20 + `NOMINMAX`(mac clang 是 `-DNOMINMAX` 或等效);`NDEBUG` 保持(与 Release wrapper ABI 一致,否则 `~RefCountedThreadSafeBase` LNK 类问题)。
5. **`_HAS_EXCEPTIONS`**:Windows 用 `_HAS_EXCEPTIONS=0`(Godot disable_exceptions);mac clang 是 `-fno-exceptions`——CEF 侧源文件的异常策略要与 wrapper 一致。
6. **SCsub 平台参数**:`env["platform"]=="macos"`、arch 判断(`arm64`/`x86_64`)、`env.msvc` 不存在(mac 是 clang)——门控和 C++20 flags 覆盖逻辑都要按平台分支。
7. **CEF SDK 平台后缀**:`macosx64` 用于 x64,macosarm64 用于 Apple Silicon——`cef_dist.py` 的 suffix 与下载 URL 模板需按平台。
8. **调试定位**:CEF `debug.log` 在 exe 所在目录(mac 上可能不同,先找日志再定位问题)。

---

## 5. 调试步骤（建议顺序）

```bash
# ① 准备:确认工具链
xcode-select --install          # Xcode 命令行工具
brew install cmake              # 或系统 cmake ≥3.19
# Godot mac 构建依赖见 Godot 官方文档(需要 scons、可选 clang)

# ② 平台适配(改这 3 处,见 §2)
#    - modules/webview/SCsub:门控 + C++20 flags + 链接(平台分支)
#    - misc/scripts/cef_dist.py:SDK_DIR_SUFFIX + 下载 URL 平台模板 + 缓存路径
#    - modules/webview/webview_core.cpp:缓存路径(LOCALAPPDATA → ~/Library/Caches)

# ③ 预构建 CEF 产物(首次/换版本)
task stage-webview               # 自动下载 macosarm64 包 → 解压 → 构建 wrapper+helper
# 或手动:CEF_DIST_ROOT=~/cef-dist-mac task stage-webview

# ④ 编引擎
task dev                         # 若 SCsub 报错,按提示修复平台分支

# ⑤ 运行验证(按 §3 清单逐项)
# 编辑器加载测试项目,观察日志:
bin/godot.macos.editor.dev.arm64 --path <cef-b0-test 项目> --editor

# ⑥ 逐项核对 §3 清单,记录每个 Gate B0-B5 的通过/失败与证据
```

---

## 6. 交付物（同事调试完提交）

1. §3 清单逐项勾选结果 + 每项证据（日志行/截图）
2. mac 平台差异的实际修复 diff（SCsub/cef_dist.py/webview_core.cpp 的平台分支）
3. 未通过项的根因分析与剩余风险
4. 若 Apple Silicon 验证 arm64,记录;若只有 x64,注明 Rosetta 场景

---

## 7. 与现有文档的关系

- 本文是《Godot编辑器UI重构方案-TS路线-CEF集成-C++生态复核与从零选型.md》的 mac 验证补充（Windows C0 已通过）。
- 平台适配改动最终合入 §C0.0b 构建契约（按平台分支扩展）。
