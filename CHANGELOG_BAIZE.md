# CHANGELOG_BAIZE.md —— baize-godot fork 定制更新记录

> 本文件记录 baize-godot（Godot Fork）相对上游的**当前定制点**，方便随时回顾我们和上游的不同修改。
> 上游基线：Godot 4.8-dev（merge-base `7a3904e22b`，2026-07 上游 master）
> 决策唯一权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_All-in-CSharp_总方案.md`
> 路线：**Godot Fork All-in C#**（C++ 引擎内核 + C# 唯一开发面，对标 Unity 模式）
> 更新约定：每新增/修改一处 fork 定制，在此追加/更新对应条目（含日期、目的）。

---

## 当前 fork 定制清单（与上游的差异点）

### 1. 规则与流程

| 文件 | 定制内容 | 引入时间 |
|---|---|---|
| `AGENTS.md` | fork 强制开发规则：All-in C# 路线、中文优先宪法、SPDX 文件头、30 秒测试时限、交互验证流程、文档索引 | 2026-08-03 起，持续更新 |
| `Taskfile.yml` | task 构建入口（dev/pro/dev-install/pro-install/dev-run） | 2026-07-23 |
| `CHANGELOG_BAIZE.md` | 本文件 | 2026-08-21 |
| `.gitignore` | 末尾清理 + fork 特定忽略（如早期工具残留） | 持续 |

### 2. 构建体系

| 文件 | 定制内容 | 引入时间 |
|---|---|---|
| `misc/scripts/build.py` | 跨平台 scons 构建包装器（preset: dev/pro，`--jobs` 支持）——替代上游 build-windows.ps1/build-macos.sh | 2026-07-23 |
| `misc/customization/scons-profiles/windows_3d_dev.py` | Windows 开发版构建配置 | 2026-07-23 |
| `misc/customization/scons-profiles/windows_3d_pro.py` | Windows 发布版构建配置 | 2026-07-23 |
| `misc/customization/scons-profiles/macos_3d_dev.py` | macOS 开发版构建配置 | 2026-07-23 |
| `misc/customization/scons-profiles/macos_3d_pro.py` | macOS 发布版构建配置 | 2026-07-23 |

### 3. 引擎核心定制（FORK-CUSTOM）

| 文件 | 定制内容 | 引入时间 |
|---|---|---|
| `core/string/ustring.cpp` / `ustring.h` | **FORK-CUSTOM**：`String(const char*)` UTF-8 智能解码——合法 UTF-8（含纯 ASCII）按 UTF-8 解码，非法序列回退 Latin-1。C++ 中文字面量直接构造即正确（中文优先宪法根基，commit b175d92bd6，经审查修复） | 2026-08-03 |
| `editor/animation/animation_track_editor.cpp` | 修复：`set_animation` 时 `imported_anim_warning->hide()`（1 行） | 早期 |

### 4. 文档

| 文件 | 定制内容 | 引入时间 |
|---|---|---|
| `doc/customization/build-profiles.md` | 构建 profile 说明 | 2026-07-23 |
| `doc/customization/getting-started-windows.md` | Windows 构建入门 | 2026-07-23 |
| `doc/customization/getting-started-macos.md` | macOS 构建入门 | 2026-07-23 |
| `doc/customization/godot-default-minus-z-forward-guide.md` | 默认 -Z 前向坐标系指南 | 2026-07-23 |
| `doc/plans/GDExtension机制澄清与选型-为什么能力层不用它.md` | GDExtension 定位澄清（能力层/脚本层不用 GDExtension） | 2026-08 |
| `thirdparty/README.md` | 无新增条目（历史上曾含 cefviewcore，已清） | — |

---

## 已放弃的探索（勿恢复，仅 git 历史）

> 以下内容**已从当前代码删除**，仅存于 git 历史。勿从历史恢复。

- **CEF WebDock 集成**（att_webview + thirdparty/cefviewcore）→ 2026-08-06 删除
- **Node sidecar 通道**（att_nodejs_sidecar + JSON-RPC）→ 2026-08-06 删除
- **editor_ops 编辑器领域能力**（att_editor_ops）→ 2026-08-06 删除
- **gd_provider**（C++ AI 对接层：WS/JSON-RPC + Registry + Ops + Events）→ 2026-08-21 退役
- **web/ TS 测试套件**（godot-rpc/godot-sdk/godot-process + Electron app）→ 2026-08-20/21 删除
- **easy_bonemap**（Python 骨骼提取工具）→ 2026-08-21 删除

---

## 规划中的定制（见总方案，尚未实现）

- 基底切换：4.7.2-stable（当前 4.8-dev）
- 目标框架：.NET 11 + C# 15（方案 B，Preview 起步）
- 脚本语言：仅 C#（产品面禁用 GDScript）
- ECS-first Runtime（Friflo.Engine.ECS）
- Scene DB 编辑器 + Avalonia UI 层
- AI 对接层：MCP 标准（Wick / MCP C# SDK）
- 三级 Reload（Level 3 ALC 已成熟，Level 1 Metadata 待做）
