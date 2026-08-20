# AI-first 对接架构：gd_provider 设计方案

> 状态：当前架构（2026-08-07 定案）
> 定位：本文档是 baize-godot fork 的**当前架构唯一事实源**——决策链、四层结构、协议契约、能力面清单、AI 对接路线均以本文档为准。改决策需先改本文档。

## 1. 背景与决策

baize-godot 是 Godot 4.8-dev 的 fork。**已放弃 Web/TS 集成**（Electron UI、CEF WebDock、
Node sidecar、TS 脚本语言、`web/ui` 旧壳均已删除，勿恢复），保留一条**面向 AI 的对接层**
`modules/gd_provider`——让外部消费方（AI Agent、CLI 工具、未来的 MCP 适配器）通过稳定协议
驱动 Godot 编辑器。

```text
外部消费方（AI / CLI / 工具）→ WS/JSON-RPC → gd_provider → Godot Core
```

**关键决策**（改决策需先改本文档）：

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

## 2. 四层结构

`modules/gd_provider/` 内部分四层，职责严格分离：

| 层 | 文件 | 职责 |
|---|---|---|
| **Ops** | `ops.cpp/.h` | 能力实现——把引擎既有 API（EditorInterface/EditorSelection/EditorUndoRedoManager/Node）组合成语义操作，含类型转换/路径守卫/只读拒绝。引擎仍是唯一执行者 |
| **Registry** | `registry.cpp/.h` | 能力注册表——方法名/描述/参数 schema/错误码/事件声明的**唯一事实源**；任何对外通道从本表查询分派（`find` + `validate_args` + `handler`） |
| **Transport** | `provider_server.cpp/.h` | 传输层——WS server + JSON-RPC 分派 + 认证（hello token）+ 预算（MAX_CLIENTS/MAX_OUT_BYTES/MAX_MSGS_PER_FRAME） |
| **Events** | `provider_server.cpp/.h`（事件源段） | 事件层——EditorSelection 信号 + EditorUndoRedoManager 信号 + 帧轮询 diff 兜底，向下行推送 |

生命周期：`register_types.cpp` 在 `MODULE_INITIALIZATION_LEVEL_EDITOR` 用
`MessageQueue` 首帧启动（编辑器核心已就绪），`uninitialize` 时 `free_singleton`。

## 3. 协议契约

- 传输：WS over TCP loopback，默认端口 `23009`（env `BAIZE_PROVIDER_PORT` 覆盖）；
- 每个 WS text message = 恰好一个 JSON-RPC 2.0 document；
- request id 一律 string；batch 显式拒绝（-32600）；server 拒 response 输入；
- 错误码：`-32601`（method_not_found）/`-32602`（invalid_params）/`-32000`（业务失败），
  内部字符串码入 `error.data.code`；
- 能力方法从 Registry 查询分派；事件下行 = notification（无 id）。

### 3.1 认证

- client 首帧 `hello`（params `{ token }`）校验；token 从 env `BAIZE_PROVIDER_TOKEN` 读；
- env 缺失 = dev 宽松模式（警告 + 跳过校验，便于本地联调）；
- 未认证连接调用非 hello 方法 → 断开；认证 deadline 3s；`hello` 幂等（已认证连接错误 token 同样拒绝）。

### 3.2 返回语义

handler 统一返回 `{ ok, result }` / `{ ok:false, error:{ code, message } }`；
JSON-RPC 映射（result/error 包装）在 ProviderServer 层完成。

## 4. 能力面清单（Registry 唯一事实源）

### 4.1 方法（17 个）

**editor.\***（编辑器状态/操作）：

| 方法 | 说明 |
|---|---|
| `editor.get_state` | 编辑器状态（场景/选中/undo 栈） |
| `editor.select_node` | 选中场景节点（与人工点击一致，走 EditorSelection） |
| `editor.undo` / `editor.redo` | 撤销 / 重做上一步 |
| `editor.save_scene` | 保存当前编辑场景（无场景 → no_scene；从未保存过 → not_saved） |
| `editor.save_scene_as` | 另存当前编辑场景到指定路径 |
| `editor.get_theme` | 编辑器主题信息（主题名/预设/基础色/强调色/字号） |
| `editor.get_scale` | 编辑器 UI 缩放比例（EDSCALE） |
| `editor.get_project_info` | 项目信息（名称/主场景/渲染器/引擎版本/路径） |

**scene.\***（场景编辑能力）：

| 方法 | 说明 |
|---|---|
| `scene.get_node_position` | 读取 Node3D 位置 `{x,y,z}` |
| `scene.set_node_position` | 设置 Node3D 位置（undo 入栈，与人工一致） |
| `scene.get_tree` | 读取编辑场景树（TreeNode 递归结构；无打开场景 → null） |
| `scene.get_props` | 读取节点属性列表（PropInfo：name/type/editable/value） |
| `scene.set_prop` | 设置节点属性（undo 入栈；值按类型解码，INT 可作 FLOAT 源） |
| `scene.create_node` | 在场景中创建节点（undo 入栈，与人工创建一致） |
| `scene.remove_node` | 从场景删除节点（undo 可恢复，含子树 owner） |

路径守卫（所有 node_path）：场景相对路径（`"."` = 根）；禁止绝对路径/`..`；必须是根自身或子孙。

### 4.2 事件（4 个，均为 notification）

| 事件 | 载荷 | 触发 |
|---|---|---|
| `editor.selection_changed` | `{ node_paths: string[] }` | EditorSelection 信号 |
| `editor.node_position_changed` | `{ node_path, position:{x,y,z} }` | 帧轮询 diff（选中 Node3D 位置变化） |
| `scene.changed` | `{ tree: TreeNode \| null }` | 树签名变化（mutation/undo 后立即 + 2s 兜底轮询） |
| `editor.undo_stack_changed` | `{ can_undo, can_redo }` | EURM history_changed/version_changed 信号 + 轮询兜底 |

## 5. 验证体系

- **单测**（`web/packages/godot-rpc`、`godot-sdk`）：协议编解码/配对/传输/绑定纯逻辑；
- **端到端**（`web/tests/e2e/provider.test.ts`，`task verify-provider`）：
  spawn headless 编辑器 + 测试套件链路，断言能力方法读写 + 错误契约。Godot 模块无单测框架，
  e2e 是 gd_provider 行为验证的可靠方式。**改动 gd_provider 必须跑**。

## 6. AI 对接路线（后续扩展方向）

能力域按里程碑扩展，每扩展一个域：Registry 注册方法 + schema → Ops 实现 → e2e 补断言。

| 能力域 | 候选方法 | 用途 |
|---|---|---|
| `project.*` | 读写 project.godot 设置 | AI 配置项目 |
| `resource.*` | 资源导入/编辑/创建 | AI 管理资产 |
| `run.*` | 运行/停止项目、读取运行日志 | AI 驱动调试循环 |
| `viewport.*` | 视口截图/像素读取 | AI 视觉反馈（需 GPU 上下文，D2） |
| `script.*` | 读取/写入/运行 GDScript | AI 写代码 |
| **MCP 适配器** | 把 Registry `methods()` 反射为 MCP tool 列表 | AI Agent 标准接入 |

**MCP 适配器（推荐路径）**：gd_provider 的 Registry 已天然适配 MCP——
`methods()` 返回的方法名/描述/schema 可直接映射为 MCP `tools/list` 的 tool 定义，
请求/响应结构与 JSON-RPC 2.0 对齐。适配器可做在 Godot 外部（独立进程经 WS 桥接），
保持 Core 零 MCP 依赖；或做在内部（新 Ops 方法），按 D6 由 Registry 统一分派。

## 7. 相关文档

- `doc/plans/GDExtension机制澄清与选型-为什么能力层不用它.md`：能力层/脚本层不用 GDExtension 的选型依据
- `AGENTS.md`：fork 强制规则（中文优先/SPDX/30 秒测试时限/交互验证流程）
- `doc/customization/`：构建指引（scons profiles 等）
- 旧架构（Electron/CEF/WebUI/NodeSidecar）文档已删除，见 git 历史，勿恢复。
