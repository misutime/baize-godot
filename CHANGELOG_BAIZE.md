# CHANGELOG_BAIZE.md —— baize-godot fork 定制流水账

> 本文件是 baize-godot（Godot Fork）相对上游定制的**流水账记录**，按时间倒序追加。
> 每笔定制：日期 + 改动 + 目的（一句话）。新增定制直接在顶部加一条，无需分类。
> 上游基线：Godot 4.8-dev（merge-base `7a3904e22b`，2026-07 上游 master）
> 决策唯一权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_All-in-CSharp_总方案.md`

---

## 2026-08-21
- 新增 `CHANGELOG_BAIZE.md`：fork 定制流水账，随时追加。
- AGENTS.md 重写为 All-in C# 路线（架构总览 D1-D6：4.7.2 基底 / .NET 11 / 仅 C# / 少自研多集成 / ECS-first + Scene DB / 三级 Reload）。
- 删除 `modules/gd_provider/`（9 文件 C++ AI 对接层）——C++ 自研违反少自研宪法，AI 对接改 MCP 标准（Wick/MCP C# SDK）。
- 删除 `web/`（27 文件 TS 测试套件）——100% 围绕 gd_provider，无独立价值。保留上游 `platform/web/`。
- 删除 `test-projects/provider/`。
- 删除 `doc/plans/AI-first对接架构-gd_provider-设计方案.md`。
- Taskfile.yml 移除 `verify-provider` + `TEST_PROJECT`，`dev-run` 简化（去掉 `--path`）。
- 删除 `tools/easy_bonemap/`（18 文件 Python 骨骼工具）——与 All-in C# 语言不一致。
- .gitignore 移除 easy_bonemap 忽略规则。
- 合并 5 份演进方案 → 1 份总方案 `Godot_Fork_All-in-CSharp_总方案.md`（v3.2，决策唯一权威）。
- GDExtension 文档清理 gd_provider 失效引用。
- thirdparty/README.md 清理历史空行。

## 2026-08-20
- 放弃 Web/TS 集成：删除 `web/app/`（Electron 宿主 + React UI + playwright e2e + vite/tsdown）。
- `web/` 瘦身为 gd_provider 测试套件（godot-rpc/godot-sdk/godot-process 三包）。
- 删除 `doc/plans/已完成-历史文档/`（CEF/OSR/NodeSidecar/WebUI 历史文档）。
- thirdparty/README.md 移除 cefviewcore 段落。
- 删除旧架构文档（ElectronUI-TSScript 设计方案、当前代码现状分析）。

## 2026-08-06
- 新架构地基：删除 CEF/Node sidecar 旧集成——`modules/att_webview`、`att_editor_ops`、`att_nodejs_sidecar`、`thirdparty/cefviewcore`、CEF 构建脚本。
- 恢复 `editor/themes/editor_fonts`、`misc/scripts/build.py` 上游版本。
- 归档旧架构文档（CEF 选型/OSR 渲染/NodeSidecar 实施记录）。
- web 包迁移命名：godot-rpc / godot-sdk / godot-process。
- justfile 移除（dev-run 并入 task）。

## 2026-08-03
- FORK-CUSTOM：`core/string/ustring.cpp/.h` `String(const char*)` UTF-8 智能解码（commit b175d92bd6）——合法 UTF-8 按 UTF-8，非法回退 Latin-1；C++ 中文字面量直接构造即正确（中文优先宪法根基）。经审查修复（commit e08c1ea0f8，P1×2 + P2×1）。
- 新文件头规则（用户裁决）：新建文件用单行 SPDX 标识，不复制上游 30 行版权块。

## 2026-07-23
- 新增 `misc/scripts/build.py`（跨平台 scons 构建包装器，preset dev/pro）。
- 新增 `misc/customization/scons-profiles/`（windows_3d_dev/pro、macos_3d_dev/pro）。
- 新增 `Taskfile.yml`（task 构建入口）。
- 新增 `doc/customization/`（构建指引 4 篇）。

## 2026-07（早期探索，已放弃）
- CEF WebDock 集成（att_webview + thirdparty/cefviewcore）→ 08-06 删除。
- Node sidecar 通道（att_nodejs_sidecar + JSON-RPC）→ 08-06 删除。
- editor_ops 编辑器领域能力（att_editor_ops）→ 08-06 删除。
- gd_provider（C++ AI 对接层）→ 08-21 退役。
- web/ TS 测试套件（godot-rpc/godot-sdk/godot-process + Electron app）→ 08-20/21 删除。
- easy_bonemap（Python 骨骼工具）→ 08-21 删除。

---

## 待办（规划中，实现后移入上方流水账）
- 基底切换 4.7.2-stable（当前 4.8-dev）
- .NET 11 + C# 15（方案 B，Preview 起步）
- 产品面禁用 GDScript
- ECS-first Runtime（Friflo.Engine.ECS）
- Scene DB 编辑器 + Avalonia UI 层
- AI 对接层：MCP 标准（Wick / MCP C# SDK）
- 三级 Reload（Level 1 Metadata）
