# Godot 编辑器 UI 重构（TS 路线）——C++ 路线构建集成方案分析

> **状态**：技术分析，待用户裁决（2026-08-02）。
>
> **问题**：webview 模块（C++ 路线，CEF 151.3.12 + vendored CefViewCore）当前通过 SCsub 内 `env.Command` 调用 CMake 构建 CefViewCore。该路径在 SCons 环境下连续踩坑（WINDIR 缺失死锁、GBK 管道解码、redirect emitter 误判库名）。是否应改为**混合构建**：CefViewCore 小源码编入 SCons，libcef_dll_wrapper + helper 预构建？

---

## 1. 结论（建议）

**建议采用混合构建**：CefViewCore 源码（`thirdparty/cefviewcore` 的 `src/Shared` + `src/CefView/CefBrowserApp`，约 20 个 cpp）编入 SCons；`libcef_dll_wrapper.lib` + `CefViewWing.exe` + CEF 运行时由 stage 脚本预构建（锁版本后一次完成）。相比现状（全量 SCsub→CMake），稳定性更高、改动路径更短，成本可控。

关键事实支撑（源码实测）：

- `CefViewWing`（helper）在 CMake 中**独立编译 `Shared/*.cpp` + `CefWing/*`，不链接 `CefViewCore.lib`**（`src/CMakeLists.txt:196-201` 只含 libcef + wrapper）——因此 CefViewCore 源码移入 SCons 与 helper 的 CMake 构建**互不依赖**。
- CefViewCore 自己的源码约 330KB，是我们 vended、会频繁修改的层——编入 SCons 可获得自然增量编译（同 `webview_core.cpp` 的处理方式）。
- `libcef_dll_wrapper` 是 CEF 官方自动生成的包装层（几百文件、随版本变），锁版本后**几乎不变**——预构建一次即稳定，无需每次 SCons 触发。

---

## 2. 现状与问题（全量 SCsub→CMake）

```text
SCsub
  └─ env.Command
       ├─ cmake -S thirdparty/cefviewcore -B ...（配置：CompilerId 探测）
       └─ cmake --build ...（构建 CefViewCore.lib + libcef_dll_wrapper.lib + CefViewWing.exe）
```

### 已踩的坑（均实测，2026-08-02）

| # | 坑 | 根因 | 修复 |
|---|---|---|---|
| 1 | **死锁**：cmake 在 CompilerId 阶段卡 10+ 分钟，cl/cmake/MSBuild CPU=0 | SCons 默认 `ENV` 缺 `WINDIR`，VS18/MSBuild 死等 | SCsub 用完整 `os.environ` 作 Command ENV（已修） |
| 2 | **编码**：GBK `UnicodeDecodeError` 使 `mySubProcess` 读线程死亡 | `methods.py:388` `Popen(text=True)` 未指定 encoding，MSBuild 输出 UTF-8/中文 | 输出重定向到日志文件规避（已修） |
| 3 | **库名误判**：`comctl32` 被加平台后缀报 LNK1181 | `redirect_build_objects` emitter 把 SCsub 阶段 LIBS 裸名当构建目标 | CEF 标准库改走 LINKFLAGS（已修） |

**本质**：这些坑全是 **SCons 环境的坑**（Godot 的 spawn/emitter 机制对"SCsub 内调外部构建系统"不友好），不是 CMake 的错。每加一个环节就多一层与 Godot 构建系统的摩擦。

---

## 3. 候选方案

### 方案 A：现状（全量 SCsub→CMake，已打补丁）

- 优点：零手动步骤，`task dev` 一步到位；cmake 增量快（3-5 秒）。
- 缺点：SCsub→cmake 的集成脆弱，每个新环境/新 CMake 版本可能再踩新坑；GBK 编码隐患未从根上消除（只是被重定向掩盖）。

### 方案 B（推荐）：混合构建

```text
SCsub 编入：CefViewCore 源码（Shared + CefViewBrowserApp，~20 cpp）
            └─ env_webview_core 同款：C++20 + NOMINMAX + WIN32_LEAN_AND_MEAN
stage 预构建：libcef_dll_wrapper.lib + CefViewWing.exe + CEF 运行时
            └─ stage_webview.py 内调 cmake 只构建 wrapper+helper 目标，产物锁版本
链接：module_webview → CefViewCore(SCons 编译) → libcef → libcef_dll_wrapper(stage 产物)
```

- 优点：
  - 消除 SCsub→cmake 的**全部** SCons 环境坑（不再有 Command 调 cmake）。
  - CefViewCore 是我们 vended 会改的层——编入 SCons 后自然增量编译，改源码只触发模块重编。
  - libcef_dll_wrapper + helper 锁版本后不变，预构建一次即稳定。
  - stage 脚本本就有"构建后暂存"职责，预构建放这里语义正确（`task stage-webview` 一次完成，不增加独立手动步骤）。
- 缺点：
  - libcef_dll_wrapper.lib 成为 stage 产物，SCsub 链接它需要版本锁定 + 缺失校验（缺时报错提示跑 stage，不静默）。
  - 换 CEF 版本需重新预构建（与现状相同）。

### 方案 C：全手动预构建

- 最稳（SCons 只链接），但改 CefViewCore 源码要重跑预构建——**不推荐**，CefViewCore 是高频修改层。

---

## 4. 方案 B 落地要点（若裁决采用）

1. **SCsub**：
   - 移除 `cef_build`/`cef_link_libs` 两个 `env.Command`（cmake 调用）。
   - 将 `thirdparty/cefviewcore/src/Shared/*.cpp` + `src/CefView/CefBrowserApp/*.cpp` 用 `env_webview_core`（C++20）编入 `modules_sources`。
   - 链接引用 stage 产物：`CefViewCore.lib`（SCons 自编，无此产物）、`libcef.lib`（SDK，直接引用）、`libcef_dll_wrapper.lib`（stage 产物路径）。
   - CEF 标准库继续走 LINKFLAGS。
2. **stage_webview.py**：
   - 增加"预构建"步骤：调 cmake 只构建 `CefViewWing` 目标（连带 `libcef_dll_wrapper`），输出 `libcef_dll_wrapper.lib` + `CefViewWing.exe` + CEF 运行时。
   - 产物校验：`libcef_dll_wrapper.lib` 缺失/版本不匹配时报错，提示跑 stage。
   - 版本锁定：`CEF_SDK_VERSION` 单点常量（现有 SCsub 已有），stage 读取同一常量。
3. **版本/许可**：沿用 §8.5 裁决（CefViewCore vendor 后与上游断开）；`libcef_dll_wrapper` 属 CEF SDK 生成代码，锁版本即可。

---

## 5. 首次构建流程（开发者视角，已实测）

```bash
# ① 预构建 CEF 产物（首次/换 CEF 版本才真正构建；产物存在则跳过，秒级）
task stage-webview
# 或：just webview-stage（同一入口）

# ② 编引擎（含 CefViewCore 源码 + 链接 stage 产物）
task dev
# 或：just dev
```

**跳过 ① 直接 ② 的行为**（已实测）：SCons 配置阶段报错，明确指引：

```
SConsEnvironmentError: [webview] CEF SDK 定位失败:CEF SDK 未缓存:<缓存路径>
请先运行 task stage-webview(自动下载 <官方 URL> 到 <缓存路径>)。
离线可用:手动下载该 tar.bz2 放到 <缓存位置>,或设置 CEF_DIST_ROOT 指向已解压的 SDK 目录。
```

- 报错时机：SCons 配置阶段（早期失败，不浪费编译时间）；`task dev` / `task pro` 同路径均生效。
- 换 CEF 版本：改 `modules/webview/SCsub` 的 `CEF_SDK_VERSION` 常量 → 跑一次 `task stage-webview`（自动下载新版本 + 重建）→ `task dev`。

## 5b. CEF SDK 缓存机制（2026-08-02 实施：依赖缓存 + 自动下载 + 手动覆盖）

SDK 定位由共享模块 `misc/scripts/cef_dist.py` 统一处理（SCsub 配置期与 stage 运行时共用单一实现）：

```text
默认缓存根:<repo>/bin/cef-dist/（git 忽略，贴近开发者）
环境变量:  CEF_DIST_ROOT=<任意位置>（CI/共享缓存/用户级，最高优先）
缓存结构:  <root>/<CEF_SDK_VERSION>/cef_binary_<CEF_SDK_VERSION>_windows64/
定位优先级: ① 已解压 SDK → ② 缓存 tar.bz2（手动放包=离线）→ ③ 自动下载官方包 → ④ 报错
下载源:    https://cef-builds.spotifycdn.com/cef_binary_<CEF_SDK_VERSION>_windows64.tar.bz2
```

- SCsub 配置期 `allow_download=False`：不联网，SDK 缺失时报错指引先跑 stage。
- stage 运行时 `allow_download=True`：缺失自动下载（打印 URL + 进度），失败明确报错。
- 已删除 `modules/webview/cef-dist.txt`（不再有机器路径进 git）。

## 6. 与现有文档的关系

- 本文件是《Godot编辑器UI重构方案-TS路线-CEF集成-C++生态复核与从零选型.md》的构建集成补充，不改变 C0 结论（CefViewCore + MessageRouter 路线）。
- 方案 B 已实施：§C0.0b 构建契约、本文件、justfile/Taskfile 注释（`webview-stage`/`dev-run`，gdext 时代 recipe 已清理）已同步。

---

## 6. 待用户裁决

- **采用方案 B（推荐）**：CefViewCore 源码进 SCons，libcef_dll_wrapper + helper 走 stage 预构建。
- **维持方案 A**：保留现状（已打补丁，能跑），接受 SCsub→cmake 的集成脆弱性。
- **方案 C**：全部预构建（不推荐，失去 CefViewCore 增量编译）。
