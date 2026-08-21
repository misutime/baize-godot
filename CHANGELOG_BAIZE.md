# CHANGELOG_BAIZE.md —— baize-godot fork 定制更新记录

> 本文件记录 baize-godot（Godot Fork）相对上游的**定制更新**，按时间倒序。
> 上游基线：Godot 4.8-dev（merge-base `7a3904e22b`，2026-07 上游 master）
> 决策唯一权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_All-in-CSharp_总方案.md`
> 路线：**Godot Fork All-in C#**（C++ 引擎内核 + C# 唯一开发面，对标 Unity 模式）

---

## 2026-08-21 —— All-in C# 路线收敛（清理阶段）

### 删除 easy_bonemap
- **删除** `tools/easy_bonemap/`（18 文件，2858 行 Python 骨骼提取/归一化工具链）
- 理由：fork 自研 Python 工具，与 All-in C# 路线语言不一致（少自研多集成宪法）
- 动画重定向方向改由成熟 C# 方案（humanoid-retargeter）承接

### gd_provider 完全退役
- **删除** `modules/gd_provider/`（9 个 C++ 文件：Ops/Registry/Transport/Events 四层 AI 对接层）
- **删除** `web/`（27 个 TS 文件测试套件，100% 围绕 gd_provider）
- **删除** `test-projects/provider/`、`doc/plans/AI-first对接架构-gd_provider-设计方案.md`
- Taskfile.yml 移除 `verify-provider` + `TEST_PROJECT`
- 理由：gd_provider 是纯自研 C++ 模块（非上游），违反"少自研多集成"宪法；
  操作对象（Node 树）被 ECS-first + Scene DB 架构淘汰；AI 对接改用 MCP 标准（Wick / MCP C# SDK）
- **保留**：`platform/web/`（上游 Web 导出平台，勿与已删 fork `web/` 混淆）

### 文档体系重构
- **合并 5 份演进方案 → 1 份总方案**（`Godot_Fork_All-in-CSharp_总方案.md` v3.2）：
  战略宪法 / 技术路线 / 架构模式 / 生态集成 / 实施路线
- AGENTS.md 重写为 **All-in C# 路线**（架构总览 D1-D6，去 gd_provider）
- GDExtension 文档清理 gd_provider 失效引用

---

## 2026-08-20 —— Web/TS 集成放弃（AI-first 清理）

### 放弃 Web/TS 集成
- **删除** `web/app/`（Electron 宿主 + React UI + preload + playwright e2e + vite/tsdown 配置）
- `web/` 瘦身为 gd_provider 测试套件（godot-rpc/godot-sdk/godot-process 三包）
- pnpm lock 重生成（-162 包：electron/biome/react/playwright 全移除）
- **删除** `doc/plans/已完成-历史文档/`（CEF/OSR/NodeSidecar/WebUI 全部历史文档）
- thirdparty/README.md 移除 cefviewcore 段落
- 决策：完全放弃 Web/TS 集成，转 Godot Core + AI 对接层

---

## 2026-08-06 —— 新架构地基定案（第 0 阶段）

- **删除** CEF/Node sidecar 旧集成：
  `modules/att_webview`（CEF 全链）、`modules/att_editor_ops`、`modules/att_nodejs_sidecar`、
  `thirdparty/cefviewcore`、CEF 构建脚本
- **恢复** `editor/themes/editor_fonts`、`misc/scripts/build.py` 上游版本
- 归档旧架构文档（CEF 选型/OSR 渲染/NodeSidecar 实施记录）
- web 包迁移命名：godot-rpc / godot-sdk / godot-process
- justfile 移除（dev-run 并入 task）
- AGENTS.md 重写为 Godot Core + Electron 架构（后于 08-21 再次重写为 All-in C#）

---

## 2026-08-03 —— 中文优先与编码规范定案

### FORK-CUSTOM：`String(const char*)` UTF-8 智能解码（b175d92bd6）
- **修改** `core/string/ustring.cpp/.h`：`String(const char*)` 从 Latin-1 语义改为**智能解码**——
  合法 UTF-8（含纯 ASCII）按 UTF-8 解码，非法序列回退 Latin-1（兼容字节透传）
- 意义：C++ 中文字面量/UTF-8 数据直接构造即正确，废除"必须 String::utf8()"硬规则
- 经审查修复（P1×2 + P2×1，commit `e08c1ea0f8`）

### 新文件头规则（用户裁决）
- 新建文件用单行 SPDX 标识（`// SPDX-License-Identifier: MIT`），不复制上游 30 行版权块
- 既有上游文件保留原版权块

---

## 2026-07-23 —— 构建体系定制（早期探索）

- **新增** `misc/scripts/build.py`（跨平台 scons 构建包装器，替代 build-windows.ps1/build-macos.sh）
- **新增** `misc/customization/scons-profiles/`（windows_3d_dev/pro、macos_3d_dev/pro 构建配置）
- **新增** `Taskfile.yml`（task 构建入口：dev/pro/dev-install/pro-install/dev-run）
- **新增** `doc/customization/`（构建指引：build-profiles/getting-started-windows/macos/
  godot-default-minus-z-forward-guide）
- 迁移构建文档、脚本和 justfile（commit `e609c408d0`）

---

## 2026-07 —— 早期 AI-first 探索（已全部放弃，仅历史记录）

以下探索**均已放弃**，仅存档于 git 历史，勿恢复：

| 时间 | 探索 | 结局 |
|---|---|---|
| 2026-07 中 | CEF WebDock 集成（att_webview + thirdparty/cefviewcore） | 08-06 删除 |
| 2026-07 中 | Node sidecar 通道（att_nodejs_sidecar + JSON-RPC） | 08-06 删除 |
| 2026-07 末 | editor_ops 编辑器领域能力（att_editor_ops） | 08-06 删除 |
| 2026-08 初 | gd_provider（C++ AI 对接层：WS/JSON-RPC + Registry + Ops + Events） | 08-21 退役 |
| 2026-08 初 | web/ TS 测试套件（godot-rpc/godot-sdk/godot-process） | 08-21 删除 |

---

## 附：当前 fork 定制清单（保留项）

| 类别 | 文件 | 说明 |
|---|---|---|
| 规则 | `AGENTS.md` | fork 强制规则（All-in C# 路线 + 中文优先 + SPDX + 30 秒规则） |
| 构建 | `Taskfile.yml` / `misc/scripts/build.py` / `misc/customization/scons-profiles/*` | 唯一构建入口 |
| 引擎定制 | `core/string/ustring.cpp/.h` | FORK-CUSTOM UTF-8 智能解码（中文优先） |
| 引擎修复 | `editor/animation/animation_track_editor.cpp` | 1 行修复：`imported_anim_warning->hide()` |
| 文档 | `doc/customization/*` / `doc/plans/GDExtension机制澄清...md` / `thirdparty/README.md` | 构建指引与澄清 |

**规划中的定制（见总方案）**：4.7.2-stable 基底切换、.NET 11 + C# 15、禁用 GDScript、
ECS-first Runtime（Friflo）、Scene DB 编辑器、Avalonia UI、MCP AI 对接层。
