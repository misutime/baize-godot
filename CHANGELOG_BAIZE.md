# CHANGELOG_BAIZE.md —— baize-godot fork 定制流水账

> 本文件是 baize-godot（Godot Fork）相对上游定制的**流水账记录**，按时间倒序追加。
> 起始日：**2026-08-21**（All-in C# 路线定案日，此前探索均已放弃且已删除，不记录）。
> 每笔定制：日期 + 改动 + 目的（一句话）。新增定制直接在顶部加一条，无需分类。
> 上游基线：Godot 4.8-dev（merge-base `7a3904e22b`，2026-07 上游 master）
> 决策唯一权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_All-in-CSharp_总方案.md`

---

## 2026-08-21（起始日）
- 新增 `CHANGELOG_BAIZE.md`：fork 定制流水账，从今日起记录与上游的差异。
- 定案 **All-in C# 路线**（决策唯一权威：`Godot_Fork_All-in-CSharp_总方案.md` v3.6）——战略宪法/技术路线/架构模式/生态集成/实施路线。
- **P0 实施完成（net11 切换）**：global.json 锁 11.0.100-preview.7；12 个引擎程序集切 net11.0 + LangVersion latest（C# 15 预览期写法）；Source Generator 保持 netstandard2.0；4 个 scons-profile 内建 mono + 禁 GDScript；site_scons 全链路 UTF-8；C# 冒烟项目 `test-projects/csharp-check` 实测通过（打印 "All-in C# 验证成功 (net11)"）。
- **基底定案：4.8-dev**（不切 4.7.2——4.8-dev 是 4.7 直系后代含全量功能 + mono 更新 + 零迁移，见总方案 §2.2）。
- **产品聚焦：风格化 3D 光谱**（覆盖 Anime NPR 三渲二 → Stylized PBR 全段，不做 2D 游戏、不做高写实 3D——见总方案 §1.3）。
- **宪法 6：先禁用后裁剪**（不用的功能不构建/不启用、源码保留，保上游合并亲和，深入定制后才物理删除）+ **风格化渲染架构 §1.4**（统一核心 + 风格化能力层 + Profile，shifu 审查定案）。
- AGENTS.md 重写为 All-in C# 路线（架构总览 D1-D6：4.8-dev 基底 / .NET 11 / 仅 C# / 少自研多集成 / ECS-first + Scene DB / 三级 Reload）。
- Taskfile.yml 精简（移除 verify-provider/TEST_PROJECT，dev-run 简化为 `--editor`）。
- `core/string/ustring.cpp/.h`：FORK-CUSTOM UTF-8 智能解码在案（中文优先宪法根基，commit b175d92bd6 + 审查修复 e08c1ea0f8）。
- `editor/animation/animation_track_editor.cpp`：`imported_anim_warning->hide()` 修复在案。
- `misc/scripts/build.py` + scons-profiles（win/mac dev/pro）+ `doc/customization/` 在案（构建体系）。

