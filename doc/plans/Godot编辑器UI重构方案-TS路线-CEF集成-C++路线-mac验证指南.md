# Godot 编辑器 UI 重构（TS 路线）——C++ 路线 mac 验证指南

> **状态**：待 mac 实机调试（2026-08-02 编写）。Windows C0 已全部通过，mac 支持需实机验证。
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

| # | 位置 | Windows（已通） | mac（需改/验证） |
|---|---|---|---|
| 1 | `modules/webview/SCsub` 平台门控 | `windows + x86_64 + env.msvc` 才继续 | **需改**：门控 + arch 判断 + 编译参数（clang） |
| 2 | `misc/scripts/cef_dist.py:30` `SDK_DIR_SUFFIX` | `"windows64"` | **需改**：`macosx64` / `macosarm64` |
| 3 | `modules/webview/webview_core.cpp:723-735` 缓存路径 | `%LOCALAPPDATA%/baize-godot/cef` | **需改**：`~/Library/Caches/baize-godot/cef`（或 `NSSearchPathForDirectoriesInDomains`） |
| 4 | CefViewWing helper | `CefWing/win/main.cpp`（exe） | CefViewCore 上游已支持：`CefWing/mac/main.mm` + `Info.plist` + entitlements（`src/CMakeLists.txt:358-362` 的 `.m/.mm`） |
| 5 | CEF SDK 包 | `cef_binary_<v>_windows64.tar.bz2` | `cef_binary_<v>_macosx64.tar.bz2` / `_macosarm64.tar.bz2`（`cef_dist.py` 的下载 URL 模板需按平台） |
| 6 | Godot 构建 | `task dev`（MSVC） | mac 需 Xcode + clang；Godot mac 构建命令不同（见 §5） |
| 7 | IME | Windows 拼音已通 | **mac 中文输入法实机验证**（候选窗/组合/提交） |
| 8 | 渲染 | 软件 OSR 已通 | 软件 OSR 理论平台无关；**GPU 路径（Metal）完全不同，本阶段不验证** |
| 9 | DLL/搜索 | libcef.dll + exe 旁 | mac 是 `Chromium Embedded Framework.framework`，helper 为 bundle；`browser_subprocess_path` 路径语义不同 |

**当前代码已预留的平台抽象**（不需重写）：
- `cef_dist.py` 的 `SDK_DIR_SUFFIX` 常量（注释写明"未来扩展 macosx64/linux64 时在此加"）
- `webview_core.cpp` 的 CEF 接线（settings/windowless/消息泵/桥）是跨平台 CEF API
- `SCsub` 门控显式报错（mac 上不会静默失败）

---

## 3. 验证点清单

### 3.1 构建（Gate B0）
- [ ] **SCsub 门控修改后**:mac 上 `scons` 配置通过(不再报 windows-only 错误)
- [ ] **cef_dist.py mac 分支**:`CEF_DIST_ROOT` 默认定位正确;自动下载 `_macosarm64` 包(或手动放包)
- [ ] **CefViewCore 编入 SCons**:24 源文件在 clang 下编译通过(C++20 + NOMINMAX 等效处理)
- [ ] **stage 预构建**:`task stage-webview` 在 mac 上构建 `libcef_dll_wrapper` + `CefViewWing`(helper bundle)成功
- [ ] **引擎链接**:`task dev` 完整构建通过,无符号/CRT/平台库错误

### 3.2 生命周期与消息泵（Gate B1）
- [ ] `CefInitialize` 成功(日志 `[webview_core] init: CEF initialized`)
- [ ] external message pump:主线程每帧 `CefDoMessageLoopWork` 正常(无饥饿/忙轮询)
- [ ] windowless browser 创建/关闭:页面加载 200、关闭无残留进程
- [ ] 退出:`CefShutdown` 干净,无崩溃/挂起

### 3.3 软件 OSR（Gate B2）
- [ ] `OnPaint` 回调产出:BGRA→RGBA 转换正确,尺寸匹配
- [ ] `ImageTexture` 显示:WebDock 面板渲染出页面内容
- [ ] resize 正常、无持续内存增长

### 3.4 JS 双向桥（Gate B3）
- [ ] `window.cefViewQuery({request,...})` 对象式调用(CEF 151 API)→ C++ `on_query` 收到
- [ ] `respond_query` 应答回到 JS `onSuccess`
- [ ] 刷新/关闭不留悬空 callback

### 3.5 IME 中文输入（Gate B4）
- [ ] mac 中文输入法(如拼音)在 `<input>` 输入正常:候选窗位置、组合序列完整
- [ ] 中英文混输无吞字、无重复提交、无 composition 残留

### 3.6 平台特有（Gate B5）
- [ ] 多实例/双开:root_cache_path 独立(`~/Library/Caches/baize-godot/cef`),无 profile 互斥
- [ ] 旧进程占用:退出后无残留 helper 进程(下次启动不因 exe/profile 锁失败)
- [ ] Apple Silicon(arm64):确认 CEF SDK 用 `macosarm64` 包;若用 Rosetta(x64)则验证 `macosx64` 包

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
