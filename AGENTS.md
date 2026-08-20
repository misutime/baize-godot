# baize-godot 项目规则（强制）

本文件是 baize-godot fork 的强制开发规则，AI 与开发者均须遵守。与全局规范冲突时，本文件优先（fork 特有约束）。

## 1. 架构总览（2026-08-07 更新：Godot Core + gd_provider AI 对接层）

**当前架构**（已放弃 Electron UI / Web / TS 脚本集成，详见
`doc/plans/AI-first对接架构-gd_provider-设计方案.md`）：

```text
外部消费方（AI / CLI / 工具）→ WS/JSON-RPC → gd_provider → Godot Core
```

- **Godot Core**：Godot 进程整体（引擎 + 编辑器核心 + 渲染服务）；
- **gd_provider**：Core 内的对外服务出口（`modules/gd_provider`），四层——**Ops**（操作层：官方 API 编排成语义用例）、
  **Registry**（能力注册表：方法/参数/错误码/事件声明，唯一事实源）、**Transport**（传输层：
  WS/JSON-RPC/认证/预算）、**Events**（事件层：diff 推送）；
- **消费方**：任意外部进程（AI Agent、CLI 工具、未来的 MCP 适配器），经 WS 直连 gd_provider。

**关键决策**（已在新架构文档固化，改决策需先改文档）：

| # | 决策 |
|---|---|
| D1 | Godot 独立进程 + IPC（WS/JSON-RPC），不做进程内直调 |
| D2 | 编辑器构建默认 `--editor` 运行；`--headless` 供无界面/CI 场景（逻辑完整） |
| D4 | 单一协议 + 类型单源（Registry），所有进程外消费方共用 |
| D5 | 数据真相一律在 Godot（三层：磁盘持久化/会话/运行）；消费方只发语义命令、收事件投影 |
| D6 | 能力面以 Registry 为唯一事实源，通道只做协议适配 |
| D8 | 传输定案：WS over TCP loopback 唯一通道，不预建替代（序列化维持 JSON） |

**GDExtension 定位**：不用作能力层/脚本层载体（原因见
`doc/plans/GDExtension机制澄清与选型-为什么能力层不用它.md`）。

**已放弃的路线（勿恢复）**：Web/TS 集成（Electron UI、CEF WebDock、Node sidecar、TS 脚本语言、
`web/ui` 旧壳）已整体放弃并删除——历史见 git，勿从历史恢复旧代码/旧文档到工作区。
`web/` 目录现仅为 gd_provider 的验证测试套件（见 §13）。

## 2. 构建流程

- **task 是唯一构建入口**（Taskfile.yml：dev/pro/dev-install/pro-install/dev-run/verify-provider）。构建逻辑在 `misc/scripts/build.py`。
- `task dev` → `build.py` → scons 构建编辑器（原版流程，无外部预构建钩子）。
- 构建产物：`bin/godot.windows.editor.*.exe` + `*.console.exe`（console 版日志直出终端，CLI/AI 驱动用）。

## 3. 测试规则（2026-08-07 更新）

**分层**：

| 层 | 方式 | 覆盖对象 |
|---|---|---|
| TS 单测 | `pnpm test`（web/ 下各包 vitest） | godot-rpc/godot-sdk 的纯逻辑（协议编解码/配对/传输/绑定） |
| 端到端集成 | `pnpm test:e2e`（web/）或 `task verify-provider` | **gd_provider（C++ Provider）行为**——Godot 模块无单测框架且依赖编辑器单例，端到端断言（spawn headless 编辑器 + 测试套件链路 + 错误契约）为可靠验证方式 |

**强制规则**：
- 改动 godot-rpc/godot-sdk 代码：必须跑对应包单测 + typecheck（`cd web/packages/<pkg> && npx vitest run && npx tsc --noEmit`）；
- **改动 gd_provider：必须跑 `task verify-provider`**（自动 spawn headless 编辑器 + 断言 + 清理进程）；前置：`task dev` 构建产物 + 测试项目（`test-projects/provider`，仓库内）；
- **测试项目一律放仓库内**（`test-projects/`），禁止项目外绝对路径（换机器失效——已踩过 refers/ 外部路径坑）；
- 新能力方法/协议变更：e2e 补断言（读写验证 + 错误契约）；新 TS 纯逻辑：补单测（协议向量/配对语义）。

## 9. Godot 测试时限（30 秒规则，强制）

打开 Godot 编辑器做验证/排障的命令（如
`./bin/godot.windows.editor.dev.x86_64.console.exe --path <项目> --editor > 日志 2>&1 &`）：

- **默认 30 秒内自动关闭**（sleep 30 → 采样日志 → Stop-Process 清理全部 godot 进程）
- 只需确认打开状态/页面加载/生成日志的场景：30 秒足够，**禁止拖到 1-2 分钟**
- 需要长时间持续的（长时间稳定性、内存增长、GPU/性能采样等）可突破 30 秒，但必须说明理由
- **每次测试后必须清理残留进程**（`Stop-Process -Name 'godot.windows*'`），残留进程污染后续测试

## 9.1 交互类验证流程（强制：禁止模拟点击窗口）

**测试中禁止模拟窗口点击/键盘输入**（SetCursorPos + mouse_event / keybd_event / SendInput 等窗口自动化输入一律不用）——已多次踩坑（2026-08-05 焦点双轨修复）：桌面全屏窗口拦截命中（Typora/终端等）、`GetAsyncKeyState` 在 Godot 主线程恒返回 0（线程无输入队列）、模拟坐标与真实布局偏差，自动化假阴性浪费大量轮次。

交互/焦点/UI 行为类验证（点击顺序、焦点转移、输入生效等）统一走**用户协助流程**：

1. **构建带日志输出的版本**：在关键决策点打印可观测标记（如 `focus-return: …`），日志量控制在不刷屏（事件触发才打）
2. **打开 Godot**（编辑器 + 目标项目，按 §9 的 30 秒规则取舍运行时长）
3. **指导用户点击/操作**：给出清晰操作序列与每步预期日志标记
4. **用户操作完，分析日志**：以日志证据 + 用户结论双重确认，再决定是否迭代

原则：Agent 觉得复杂、脆弱或已多轮试错的操作，**主动请求用户协助**（说明步骤与预期日志标记）——用户亲测几秒即可完成，比 Agent 闭门造车模拟输入高效得多。需要长时间观察的场景可要求用户操作后保持窗口，Agent 后台采样日志。

## 11. 新文件头规则（2026-08-03 用户裁决）

**新文件用单行 SPDX 标识**，不复制 Godot 上游的 30 行长版权块（占用顶部空间、妨碍读码）：

```cpp
// SPDX-License-Identifier: MIT
```

- 适用：本仓库所有**新建**的 C++ 头/源文件（web/ TS 工程文件按各自生态惯例，TS 可省或同用 SPDX 单行）
- **既有文件不动**：Godot 上游文件保留原版权块
- 合规依据：MIT 许可由 SPDX 标识 + 仓库 LICENSE 文件满足；Godot 上游 4.x 新文件也逐步转
  SPDX 单行风格

## 12. 编码规范与中文优先（2026-08-03 用户裁决）

**fork 立场（用户裁决）**：本仓库是 Godot 的 fork——**不被上游历史负担束缚**。上游为兼容/性能保留的旧契约（如 `String(const char*)` 的 Latin-1 语义）与中文优先冲突时直接改进（标注 FORK-CUSTOM），不必因"上游没这么做"而妥协。

**中文优先（用户裁决）**：项目第一语言是中文——代码/日志/文档/UI 的字符串默认中文；全链路 UTF-8。

**全链路 UTF-8**（外部消费方 ↔ Godot ↔ 协议 JSON ↔ 文件），防乱码关键：

- **FORK-CUSTOM（b175d92bd6）**：`String(const char*)` 已改为**智能解码**——合法 UTF-8（含纯 ASCII）按 UTF-8 解码，非法序列回退 Latin-1（兼容字节透传）。C++ 中文字面量/UTF-8 数据直接构造即正确——**旧硬规则"必须 String::utf8()"已废除**；显式 `String::utf8()` 仍可用于强制 UTF-8 语义（外部二进制等）
- 转出给外部（AI/CLI/文件）用 `.utf8()`（CharString）
- 协议 JSON：`JSON::stringify/parse_string` 默认 UTF-8
- 文件/页面：Godot 文本默认 UTF-8；HTML 必须带 `<meta charset="utf-8">`
- 日志：中文日志可直接写（String 构造已 UTF-8）；显示依赖终端代码页——Godot 输出面板/UTF-8 终端正常，GBK 控制台需 `chcp 65001`（显示层，非数据问题）

## 13. web/ 测试套件规范（2026-08-07 更新：非 UI，仅验证套件）

`web/` 目录**不是应用**，是 gd_provider 的验证测试套件（Electron/UI/TS 脚本已放弃）。

- **目录结构**：`web/packages/` 放被 e2e 消费的库——
  `packages/godot-rpc`（@baize/godot-rpc，JSON-RPC 契约 + 传输核心：类型/配对/ws 实现，零依赖）、
  `packages/godot-sdk`（@baize/godot-sdk，能力面客户端：方法绑定 + 事件订阅，依赖 rpc）、
  `packages/godot-process`（@baize/godot-process，GodotClient：WS 连接 + 认证握手 + 生命周期，依赖 rpc）。
  消费关系：e2e（`web/tests/e2e/`）→ godot-sdk/godot-process → godot-rpc。
- **禁止使用 `baseUrl`**：TS 5.0 起已弃用（`ignoreDeprecations` 仅静默到 6.0），TS 7.0 将移除。
  无 `baseUrl` 时 `paths` 相对 tsconfig.json 所在目录解析，新增别名直接写相对条目。
- 验证命令：各包 `npm run typecheck`（`tsc --noEmit`）；e2e = `task verify-provider`（等价 `pnpm --dir web run test:e2e`）。

## 14. 文档索引

- **当前架构**：`doc/plans/AI-first对接架构-gd_provider-设计方案.md`（决策链 D1-D8、四层结构、协议契约、能力面清单、AI 对接路线）
- **GDExtension 澄清**：`doc/plans/GDExtension机制澄清与选型-为什么能力层不用它.md`
- 历史：Electron/CEF/WebUI 等旧架构文档已删除，见 git 历史，勿恢复。
