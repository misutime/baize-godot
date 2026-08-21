# CHANGELOG_BAIZE.md —— baize-godot fork 定制流水账

> 本文件是 baize-godot（Godot Fork）相对上游定制的**流水账记录**，按时间倒序追加。
> 起始日：**2026-08-21**（All-in C# 路线定案日，此前探索均已放弃且已删除，不记录）。
> 每笔定制：日期 + 改动 + 目的（一句话）。新增定制直接在顶部加一条，无需分类。
> 上游基线：Godot 4.8-dev（merge-base `7a3904e22b`，2026-07 上游 master）
> 决策唯一权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_All-in-CSharp_总方案.md`

---

## 2026-08-21（起始日）
- 新增 `CHANGELOG_BAIZE.md`：fork 定制流水账，从今日起记录与上游的差异。
- 定案 **All-in C# 路线**（决策唯一权威：`Godot_Fork_All-in-CSharp_总方案.md` v3.2）——战略宪法/技术路线/架构模式/生态集成/实施路线。
- AGENTS.md 重写为 All-in C# 路线（架构总览 D1-D6：4.7.2 基底 / .NET 11 / 仅 C# / 少自研多集成 / ECS-first + Scene DB / 三级 Reload）。
- Taskfile.yml 精简（移除 verify-provider/TEST_PROJECT，dev-run 简化为 `--editor`）。
- `core/string/ustring.cpp/.h`：FORK-CUSTOM UTF-8 智能解码在案（中文优先宪法根基，commit b175d92bd6 + 审查修复 e08c1ea0f8）。
- `editor/animation/animation_track_editor.cpp`：`imported_anim_warning->hide()` 修复在案。
- `misc/scripts/build.py` + scons-profiles（win/mac dev/pro）+ `doc/customization/` 在案（构建体系）。

---

## 待办（规划中，实现后移入上方流水账）
- 基底切换 4.7.2-stable（当前 4.8-dev）
- .NET 11 + C# 15（方案 B，Preview 起步）
- 产品面禁用 GDScript
- ECS-first Runtime（Friflo.Engine.ECS）
- Scene DB 编辑器 + Avalonia UI 层
- AI 对接层：MCP 标准（Wick / MCP C# SDK）
- 三级 Reload（Level 1 Metadata）
