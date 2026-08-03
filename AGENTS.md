# baize-godot 项目规则（强制）

本文件是 baize-godot fork 的强制开发规则，AI 与开发者均须遵守。与全局规范冲突时，本文件优先（fork 特有约束）。

## 1. CEF 集成分层（结构性约束，勿尝试绕过）

webview 模块的 CEF 集成有**编译期强制的分层**，任何改动不得破坏：

```text
modules/webview/
├── webview_core.h     ← API 面：纯 C++（std::string/std::function，零 Godot 类型）
├── webview_core.cpp   ← CEF 专属 TU：必须 include CEF 头，禁止 include 任何 Godot 头
├── web_panel/webview_manager/register_types  ← Godot 壳层：可用 Godot 设施（print_line 等），只 include webview_core.h
```

### 1.1 TU 冲突规则（硬约束，实测 C2365）
- Godot 的 `core/typedefs.h` → `error_list.h` 定义 `enum Error`（含 `ERR_OUT_OF_MEMORY` 等成员）
- CEF 的 `include/internal/cef_types.h` → `cef_net_error_list.h` 定义 `net_error` 枚举（成员同名）
- **两者在同一编译单元共存必然 C2365 重定义，与 include 顺序无关**
- 因此：**webview_core.cpp（及任何 include CEF 头的 TU）禁止 include 任何 Godot 头**（含全经 typedefs.h 链的一切）
- 核心层日志/计时用标准库（stderr / std::chrono）；Godot 侧日志由壳层负责
- 壳层 TU 只 include `webview_core.h`（纯 C++），不 include CEF 头，故可用 Godot 设施

### 1.2 API 边界规则
- `webview_core.h` 保持纯 C++：回调经 `std::function`，禁止 Godot 对象穿越
- Godot 对象不进入 CefViewCore delegate；CEF 对象不穿透到 WebPanel 产品 API
- 回调在主线程（pump 内）同步触发；paint 缓冲仅回调期间有效，宿主必须拷贝

## 2. 构建流程（首次顺序强制）

```bash
# ① 预构建 CEF 产物（首次/换 CEF 版本才真正构建；产物存在则跳过，秒级）
task stage-webview        # 或 just webview-stage
# ② 编引擎
task dev                  # 或 just dev
```

- 跳过 ① 直接 ②：SCons 配置阶段报错并提示先跑 stage（不静默）——正常行为
- 换 CEF 版本：改 `modules/webview/SCsub` 的 `CEF_SDK_VERSION` 常量 → 跑 stage（自动下载新版本 + 重建）→ dev
- 首次克隆：`git clone` 后无 `bin/`（gitignore），需先 `task stage-webview`（自动下载 CEF SDK）

## 3. CEF SDK 缓存机制（依赖缓存 + 自动下载 + 手动覆盖）

- 默认缓存根：`<repo>/bin/cef-dist/`（git 忽略）
- 环境变量覆盖：`CEF_DIST_ROOT=<任意位置>`（CI/共享，最高优先）
- 缓存结构：`<root>/<CEF_SDK_VERSION>/cef_binary_<CEF_SDK_VERSION>_windows64/`
- 定位优先级：① 已解压 SDK → ② 缓存 tar.bz2（手动放包=离线）→ ③ 自动下载 → ④ 报错
- 定位逻辑统一在 `misc/scripts/cef_dist.py`（SCsub 与 stage 共用，禁复制）
- **CEF 版本锁定**：`CEF_SDK_VERSION` 只在 `modules/webview/SCsub` 定义（单点），开发者不能指定版本

## 4. 构建系统坑（已踩，勿重蹈）

### 4.1 SCsub 调外部构建
- 当前形态：CefViewCore 源码（`thirdparty/cefviewcore`）**编入 SCons**；`libcef_dll_wrapper.lib` + `CefViewWing.exe` + CEF 运行时由 **stage 预构建**（不随每次 scons）
- SCsub 配置期**不联网、不下载**（`allow_download=False`）；下载只发生在 stage

### 4.2 Godot SCons 环境坑（本仓库独有的触发点）
- **WINDIR 缺失**：SCons 默认 ENV 精简，缺 WINDIR 会让 cmake/MSBuild 在 CompilerId 阶段死等——外部构建命令必须用完整 `os.environ`
- **redirect_build_objects emitter**：SCsub 阶段 `LIBS` 里的裸库名会被误判为构建目标（加平台后缀报 LNK1181）——系统库必须走 `LINKFLAGS`
- **mySubProcess 编码**：`methods.py` 的 `Popen(text=True)` 无 encoding，GBK 解码海量 UTF-8 输出会死——大输出命令重定向到文件或不经 SCons
- 这些坑只影响"SCsub 内调外部构建系统"，Godot 第三方库（Jolt/Embree）源码编入不受影响

## 5. CefViewCore 修改授权（vendor 断开上游）

- `thirdparty/cefviewcore` 已 vendor（上游 commit `6d4a405252be014b2bb72c1f39fa6c03f416daf1`，MIT），**与上游断开，可自由修改**
- 修改时保留 LICENSE；升级 CEF 版本时自维护 diff
- **CEF 官方层不改**：`refers/cef` 源码树（不构建，成本巨大）与 `cef-dist/` 预编译 SDK（黑盒二进制）都是只读依赖，通过锁版本管理

## 6. CEF 编译要求（已实测）

- CEF 151 头文件必须 **C++20**（`convertible_to` concept）+ `NOMINMAX` + `WIN32_LEAN_AND_MEAN`
- **NDEBUG 必须**（CEF 侧源文件）：Release wrapper 把 `~RefCountedThreadSafeBase` 内联为 `= default`，Godot dev 构建不定义 NDEBUG 时 `DCHECK_IS_ON()=true` 引用外部析构 → LNK2019
- CEF 静态库 CRT 必须与 Godot 一致：Godot 默认 `/MT`（`use_static_cpp=True`），stage 预构建 `-DSTATIC_CRT=ON`
- 每个浏览器独立 `CefViewBrowserClient` + delegate；OSR 用 `windowless_rendering_enabled + external_begin_frame_enabled`
  （**2026-08-03 修复后为 internal_begin_frame**：`external_begin_frame_enabled=0`，CEF 内部帧源按
  `windowless_frame_rate=60` 驱动，宿主每帧 `CefDoMessageLoopWork` 泵送，不再 `SendExternalBeginFrame`）

## 7. 平台支持现状

- **Windows x86_64 MSVC** 与 **macOS arm64/x64（clang）** 双平台（`modules/webview/SCsub` 平台门控，其他平台显式报错）
- mac 实机验证记录：`doc/plans/Godot编辑器UI重构方案-TS路线-CEF集成-C++路线-mac验证指南.md`；
  internal_begin_frame 修复（2026-08-03）为共享代码，**mac 需复验**
- `cef_dist.py` 的 `sdk_dir_suffix()` 按宿主自动判定（windows64/macosarm64/macosx64）

## 9. Godot 测试时限（30 秒规则，强制）

打开 Godot 编辑器做验证/排障的命令（如
`./bin/godot.windows.editor.dev.x86_64.console.exe --path <项目> --editor > 日志 2>&1 &`）：

- **默认 30 秒内自动关闭**（sleep 30 → 采样日志 → Stop-Process 清理全部 godot/CefViewWing 进程）
- 只需确认打开状态/页面加载/生成日志的场景：30 秒足够，**禁止拖到 1-2 分钟**
- 需要长时间持续的（长时间稳定性、内存增长、GPU/性能采样等）可突破 30 秒，但必须说明理由
- **每次测试后必须清理残留进程**（`Stop-Process -Name 'godot.windows*','CefViewWing'`），
  残留双开会触发 CEF 同 root 单例冲突（CefInitialize failed）并污染后续测试

## 10. 文档索引

- 方案总览：`doc/plans/Godot编辑器UI重构方案-TS路线-CEF集成-C++生态复核与从零选型.md`
- mac 验证：`doc/plans/Godot编辑器UI重构方案-TS路线-CEF集成-C++路线-mac验证指南.md`
- GPU 验证：`doc/plans/验证计划-CEF-GPU加速-Win先行.md`
- 第二日实施：`doc/plans/实施计划-第二日-双向桥与输入交互.md`
- 历史（已归档）：`doc/plans/已完成-历史文档/`（构建集成方案分析、E0-CEF验证、RouteB-分发边界说明等）
