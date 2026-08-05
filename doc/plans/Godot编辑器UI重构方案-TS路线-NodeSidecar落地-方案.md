# Godot 编辑器 UI 重构（TS 路线）——Node Sidecar 落地计划

> **状态**：落地计划（2026-08-04）。衔接《TS-CEF嵌入与NodeSidecar-设计.md》（§1.2 工具链 / §4 三端两两直连 / §5.4 生命周期）、
> 《TS路线-WebUI架构-桥协议与前端SDK.md》（CEF↔Godot 通道已落地）、
> 《实施记录-AI-FIRST-P1-P2-语义接口与MCP.md》（能力面注册表 + MCP 暴露层已落地）。
> **本文回答**：Node sidecar 在 baize-godot 仓库怎么落地——目录、通道、协议、生命周期、分阶段验收。
> 只做规划；落地执行按本文 S0-S2 逐阶段推进，每阶段独立可验收。
>
> 证据标注：仓库事实标注路径；推断标 `[INFERENCE]`。引用《设计》= CEF 设计文档，《桥协议》= 桥协议与前端 SDK 文档。

---

## 1. 现状与缺口

### 1.1 已落地（2026-08-04 核实）

| 组件 | 位置 | 状态 |
|---|---|---|
| CEF 接入（CefViewCore + OSR + 输入转发 + IME） | `modules/webview/`（webview_core / web_panel / editor_web_dock / cef_application_mac） | ✅ MVP1 完成，MVP2 实机验收 4 条进行中（《桥协议》§6） |
| CEF↔Godot 进程内桥（方法注册表 + req_id 配对 + 事件源 + 字体体系） | `modules/webview/web_bridge.{h,cpp}` | ✅ 方法 10 个、事件 6 个；事件源目前**单浏览器目标**（`set_event_browser_id`） |
| 前端 workspace（sdk + ui） | `web/`（@baize/ui-sdk + @baize/ui，React 19 + Vite 8 + Tailwind） | ✅ sdk 13 单测通过；产物经 `misc/scripts/stage_ui.py` 进 `bin/webview/ui/` |
| 语义能力面（注册表 + 语义操作 + 语义 UI 树） | `modules/ai/semantic_registry.{h,cpp}` + `semantic_ops` + `editor_ui_tree` | ✅ 唯一事实源：方法名/描述/schema/handler 集中注册 |
| MCP 暴露层（HTTP server） | `modules/ai/ai_bridge.{h,cpp}` | ⚠️ **实验品，无实际使用**（用户确认 2026-08-04）：127.0.0.1:47653（`AI_BRIDGE_PORT`），token 鉴权，11 工具（ui.*/editor.*/scene.*）；定位 = 能力面活体参考实现，**S3 随 sidecar MCP server 落地后退役**（§5.2/§9-5） |

### 1.2 缺口（sidecar 未落地）

- **无 sidecar 进程**：仓库无 `runtime/`、无 Node sidecar 代码、无 spawn/生命周期管理（grep 全仓库 `sidecar` 仅设计文档提及）；
- **无 NodeJS↔Godot 通道**：《设计》§4 裁决的 WebSocket/JSON-RPC 未实现；引擎能力目前只有两条暴露路径——CEF 进程内桥（`web_bridge`）与 MCP HTTP（`ai_bridge`），**无进程外双向通道**（MCP HTTP 无服务端推送，SSE 事件面暂缓）；
- **无 CEF↔NodeJS 通道**：页面只能调 Godot，不能直连 sidecar 服务（Agent/LSP/工具链）；
- **能力面未合流**：`web_bridge` 方法注册表与 `SemanticRegistry` 各自维护（`semantic_registry.h` 注释明示"WebBridge 委托列为后续"）。

### 1.3 本文交付边界

- **范围**：sidecar 工程落地（S0）→ NodeJS↔Godot 通道（S1）→ CEF↔NodeJS 直连 + 事件多目标化（S2）→ 首个真实服务（S3）→ 发布运维（S4）。S0-S2 构成"sidecar 三端直连落地"核心；S3 服务选型留用户裁决（§9）；
- **不涉及**：不改 Godot 状态权威模型（场景树/属性/UndoRedo 仍在引擎）；不做 WebBridge→SemanticRegistry 委托重构（列为后续，避免与本计划耦合）；不做 GPU 加速渲染（独立计划）。

---

## 2. 目标与边界

**一句话**：在编辑器进程内补一条 NodeJS sidecar 通道（Godot 为 WS server + spawn 管主，sidecar 为进程外服务宿主），
实现《设计》§4 的"三者两两直连"拓扑，并以 Agent/LSP/资产管线等服务证明进程边界价值。

| 项 | 结论 |
|---|---|
| sidecar 职责 | 工具链/插件运行时：Agent 服务、LSP 查询、资产管线、npm 生态工具宿主（《设计》§1.2/§5.7） |
| 状态边界 | **不缓存引擎状态**（《设计》§4 约束 1）；sidecar 只发命令、收事件，Godot 唯一权威 |
| 连接拓扑 | 两两直连：CEF↔Godot 进程内桥（已落地）；NodeJS↔Godot WS/JSON-RPC（**本文 S1**）；CEF↔NodeJS WS（**本文 S2**） |
| 生命周期 | Godot 管 spawn/重启/退出（《设计》§5.4）；开发期支持外部 `tsx watch` 自管（HMR），Godot 只连不 spawn |
| 首个服务 | 候选：Agent 服务（推荐，§8 S3）/ LSP worker / 资产管线——**用户裁决** |

**为什么不做"CEF→NodeJS→Godot 链式"**：沿用《设计》§4 裁决——核心 Selection/Property 交互绕 NodeJS 两跳且引入一致性风险；NodeJS 只有自己的两条边。

---

## 3. 技术栈与目录裁决

### 3.1 技术栈（沿《设计》§1.2 裁决，落地时实测版本）

| 层 | 选择 | 说明 |
|---|---|---|
| Runtime | **Node.js 26 LTS（目标）** | 《设计》已裁决。当前实测环境：mac v24.18.0、Win 24.14.0（《sdk与ui-workspace》§1）——`web/runtime` 声明 `engines: ">=24"`，目标 26 LTS，`.nvmrc` 固定 |
| TS 运行（开发） | **tsx**（watch 模式 = sidecar HMR） | 与 sdk/ui 工程一致，无 Bun 依赖 |
| 构建 | **tsdown**（按边界多产物） | 《设计》§1.2：发布输出 agent-runtime.js / asset-worker.js / lsp-worker.js，不 bundle 单文件 |
| 打包（发布形态） | **Node SEA 单文件** | **用户裁决（2026-08-04）：发布不要求用户有 Node 运行时**——按服务边界（agent-runtime / asset-worker / lsp-worker）各出 SEA 自包含可执行（Node 20+ 官方 Single Executable Applications；成熟度落地实测）；开发期用本机 Node 24.x |
| 测试/Lint | **Vitest + Biome** | 复用 `web/` workspace 既有配置 |
| 通信（Node 侧） | **ws** + **vscode-jsonrpc** | ws（8.21.x，MIT）= WS 传输事实标准，client+server（sidecar 两个 WS 面）；vscode-jsonrpc（9.x，MIT，微软）= RPC over streams，LSP 同源——WS 与子进程 stdio 共用一套 MessageConnection；方法分派/业务分发自研薄层（§3.2 jsonrpc.ts） |
| 校验 | **zod（仅 sidecar 自有服务）** | 引擎能力面参数 schema 唯一事实源 = C++ `SemanticRegistry`（JSON Schema），zod **不**另起一套（防双份 schema 漂移）；仅用于 Agent 会话等 sidecar 自有输入 |
| 日志 | **pino** | JSON lines → stdout/stderr → Godot 日志（S1：`user://logs/sidecar.log`） |
| Native 模块 | Rust + napi-rs（后续按需） | 《设计》§1.2；首阶段无 native 需求 |

### 3.2 目录裁决：并入现有 `web/` workspace（不建根级独立 workspace）

```
web/                                ← 现有 TS workspace（pnpm；sdk + ui）
├── packages/
│   └── rpc/                        ← @baize/rpc：三端共享 JSON-RPC 消息类型（信封/方法/事件 payload）
└── runtime/                        ← @baize/sidecar：Node sidecar 主进程（服务宿主）
    ├── src/
    │   ├── index.ts                ← 入口：启动 → 连 Godot WS → 起 CEF 面 WS server → 加载服务
    │   ├── godot-client.ts         ← NodeJS↔Godot WS/JSON-RPC 客户端（token 握手 + req_id 配对）
    │   ├── cef-server.ts           ← CEF↔NodeJS WS server（本机回环 + token）
    │   │   ├── jsonrpc.ts              ← vscode-jsonrpc 适配 + 方法分派 + {ok,result} 映射（信封类型共享 @baize/rpc）
    │   ├── lifecycle.ts            ← 优雅退出/重启钩子、子服务注册
    │   └── services/               ← 服务边界：agent / lsp / asset（S3 起）
    └── test/
```

**决策理由**：
- 单一 pnpm lockfile + 单一 biome/tsconfig 体系；根级第二个 workspace 会引入第二套 node_modules/lockfile，且 `node_modules` 可能污染引擎 SCsub 源码扫描（`web/` 已隔离）；
- 与《设计》未来 monorepo 的映射：`ui/`=web/ui、`runtime/`=web/runtime、`packages/rpc/`=web/packages/rpc——目录名平移，路径加深一级；
- `@baize/rpc` 是纯类型包（零运行时依赖），sdk 与 runtime 共同消费；CEF 侧（`web/sdk`）与 C++ 侧（`modules/webview`/`modules/ai`）消息类型对齐（类型漂移 CI 列为后续，《设计》§5.3）。

---

## 4. 架构：三通道拓扑与角色

### 4.1 拓扑（落地后形态）

```
┌──────────────────────────────────────────────────────────────────┐
│                        Godot 编辑器进程                             │
│  ┌────────────────────┐        ┌──────────────────────────────┐  │
│  │ modules/ai         │        │ modules/webview             │  │
│  │ SemanticRegistry   │        │ web_bridge（CEF 进程内桥）    │  │
│  │ sidecar_server(新)  │        │  + 事件源（多目标化，S2）      │  │
│  │ ai_bridge(MCP HTTP)│        └──────────────┬───────────────┘  │
│  └─────────┬──────────┘                       │ CEF 进程内         │
│     WS server 47654（新）                      │（已落地）          │
└────────────┼──────────────────────────────────┼───────────────────┘
             │ spawn env 传 token/端口/项目路径    │
             │ (sidecar 主动连回 Godot)           │
    ┌────────┴──────────────────┐      ┌─────────┴──────────┐
    │ Node sidecar（web/runtime）│◄────►│ CEF 页面（web/ui）  │
    │ Agent/LSP/资产服务宿主      │  WS  │ sdk services.*    │
    │ WS server（CEF 面）        │ 直连  │ （S2，新 transport）│
    └───────────────────────────┘      └────────────────────┘
```

> 注：`ai_bridge`（MCP HTTP）为实验品（用户确认 2026-08-04，无实际使用），S3 随 sidecar MCP server 落地后退役——终态不含它（§5.2/§9-5）。

### 4.2 角色与通道

| 通道 | 方向/角色 | 传输 | 状态 |
|---|---|---|---|
| CEF↔Godot | 编辑器核心交互（高频低延迟） | CefViewClient.invoke + TriggerEvent（进程内） | ✅ 已落地 |
| **NodeJS↔Godot** | 引擎能力调用（批量导入/场景操作/agent 命令）+ 事件订阅 | **WS/JSON-RPC**（Godot = server，sidecar = client，S1 新建） | ⬜ S1 |
| **CEF↔NodeJS** | UI 调服务（AI 面板/工具链/LSP） | **WS**（sidecar = server，CEF = client，S2 新建） | ⬜ S2 |

**角色裁决**：Godot = 状态权威 + 生命周期管主（spawn/重启/退出）；sidecar = 服务宿主（无权威状态）；
CEF = 表现层。连接建立后数据面直连（VS Code main-process 模式，《设计》§4）。

### 4.3 握手与令牌链（单令牌/编辑器实例）

```
1. Godot 启动 → 生成 token（随机 32B hex，仅存内存）
2. Godot spawn sidecar：env 传 BAIZE_GODOT_WS_URL（ws://127.0.0.1:47654）、
   BAIZE_SIDECAR_TOKEN、BAIZE_PROJECT_PATH、BAIZE_SIDECAR_PORT（CEF 面端口，默认 47655）
3. sidecar 连 Godot WS → 首帧 auth：{ method:"sidecar.hello", token }（不对→ Godot 拒绝并告警）
4. sidecar 起 CEF 面 WS server → 经 Godot 事件 sidecar_ready 上报 { cef_ws_url, cef_ws_token }
5. CEF 页面经 WebBridge 新方法 sidecar.get_info 取 { url, token } → sdk 建立直连
```

- 单令牌理由：两个通道信任域相同（本机回环 + 编辑器场景）；Godot 是唯一分发点（env → sidecar，桥方法 → CEF），token 不落磁盘、不进日志（`[INFERENCE]` 威胁模型：同机恶意进程本可读 env/内存，令牌只防浏览器页面/跨源访问）；
- 端口策略沿 `AI_BRIDGE_PORT` 既有模式（《AI-FIRST》§3.7）：显式 env 可配、默认值、bind 失败清晰报错（显示端口 + 来源 + 改法），**不静默回退、不自动换端口**；双实例端口冲突与 CEF cache 独占问题（《AI-FIRST》§3.7 已记录，webview 侧同事修复中）在本计划沿用同一处理口径。

### 4.4 生命周期（《设计》§5.4 落地）

| 项 | 方案 |
|---|---|
| 启动 | 编辑器（editor 模式）启动时 spawn（懒加载备选：首次 sidecar 服务被调用时，S4 后评估）；`BAIZE_SIDECAR=0` 关闭、`BAIZE_SIDECAR=dev` 外部自管（Godot 只连不 spawn） |
| 连接 | 启动后重试握手（退避 500ms×2，上限 10 次），超时告警不阻塞编辑器（sidecar 是增强不是命门） |
| 崩溃恢复 | Godot 侧检测 WS 断开 → 若进程仍活着先杀 → 自动重启（退避，单次会话上限 3 次）；重启后重新握手 + 事件重订阅 |
| 退出 | 编辑器退出钩子顺序（《设计》§5.4）：UI 面板 → CEF → sidecar（发 shutdown 通知 + 等 2s + kill 进程树）→ 引擎；无残留进程（Win `taskkill /T` / mac 进程组） |
| Node 发现 | 开发期：`BAIZE_NODE` env > PATH 中 node，找不到时明确报错（含安装指引），不静默降级。发布期：SEA 自包含，无 Node 依赖（§3.1，已裁决） |

---

## 5. 协议：NodeJS↔Godot WS/JSON-RPC 通道（S1 核心）

协议层三件事：**信封与映射**（§5.1，线上格式）→ **能力面**（§5.2，方法语义唯一事实源）→ **C++ 实现**（§5.3，传输/分派/安全）。

### 5.1 信封与映射规则（`@baize/rpc` 定义，三端共享类型）

**传输层信封 = 标准 JSON-RPC 2.0**（沿《设计》§1.2，与 LSP 同源）：
- request 带 `id`（string，SDK 生成——规避 C++ 数字→double 陷阱，与现有 req_id 约定一致）；
- response = 标准 `result` / `error{code,message}`（JSON-RPC 数值错误码，内部码入 `error.data`）；
- **通知**（无 id）：事件下行（Godot→sidecar）与服务进度（sidecar→CEF）。

**业务语义 = `{ ok, result }` / `{ ok:false, error:{code,message} }`**（与 WebBridge/SemanticRegistry 返回格式一致）——这是 handler 的返回值语义，**由 C++ sidecar_server 分派层做一次映射**：

```
SemanticRegistry handler 返回 { ok:true,  result }  → JSON-RPC result = result
SemanticRegistry handler 返回 { ok:false, error }   → JSON-RPC error = { code, message }（内部码入 data）
```

- 映射放 C++ 分派层的理由：**Godot 是语义权威，一次映射三端受益**——CEF 桥（method_result 事件）、sidecar WS、MCP 三端语义一致，Node 侧/前端不重复实现映射；
- 选标准实现（而非自定义 `{ok,result}` 信封）的原因：`vscode-jsonrpc`（Node）与 Godot 内置 `JSONRPC` 类（`modules/jsonrpc`）都是标准 JSON-RPC 实现，可直接消费、LSP 同源；
- 消息类型：`RpcRequest/RpcResponse/RpcError/RpcNotification` + 方法/事件 payload 类型，全部在 `web/packages/rpc` 一处声明；sdk 与 runtime 从包导入；C++ 侧（sidecar_server）手写对齐 + 类型漂移校验 CI（后续）。

### 5.2 能力面：SemanticRegistry 为唯一事实源

`sidecar_server`（C++，新，位于 `modules/ai/`）**不分发到 WebBridge**，而是直接查询 `SemanticRegistry`（方法元数据 + `validate_args` + handler）——与 `ai_bridge` 的 MCP 工具面同源：

| 消息 | 语义 | 分发 |
|---|---|---|
| `scene.*` / `editor.*` / `ui.*`（SemanticRegistry 方法） | 引擎能力调用 | `SemanticRegistry::find` + `validate_args` + handler（与 MCP tools/call 完全一致） |
| `sidecar.hello` / `sidecar.health` | 握手/健康检查 | sidecar_server 自身 |
| `sidecar.subscribe` / `unsubscribe`（事件名列表） | 事件订阅 | sidecar_server 维护订阅表（S2 事件多目标化后生效） |
| `editor.selection_changed` 等（通知） | Godot→sidecar 事件下行 | 事件源 fan-out（S2） |

**`ai_bridge` 定位与退役路径**：`ai_bridge` 是实验品（用户确认 2026-08-04，无任何实际使用）——其价值是能力面的**活体参考实现**：验证 SemanticRegistry 语义 + 排坑清单（《AI-FIRST》§4 已归档，删代码不丢知识）。S1 的 WS 通道可用它交叉验证（同一能力面、两种传输、结果应一致）。**S3 做 Agent 服务时，sidecar 内以 Node MCP SDK 起 MCP server（streamable HTTP，端口可沿用 47653 但无兼容负担），面板 Agent 与外部客户端共用同一宿主；验收后删除 `ai_bridge.{h,cpp}`**，`modules/ai` 收敛为 SemanticRegistry + semantic_ops + editor_ui_tree + sidecar_server。不新增第二份能力实现：sidecar_server 直接查询 `SemanticRegistry`（与 ai_bridge 的 MCP 工具面同源，方法实现只写一份）。

### 5.3 C++ 实现要点（`modules/ai/sidecar_server.{h,cpp}`）

- **传输**：复用 `modules/websocket`（引擎内置 wslay）：`TCPServer::listen` + `WebSocketPeer::accept_stream` + 每帧 `poll`——模式参照已在本 fork 编辑器验证的 `EditorDebuggerServerWebSocket`（`modules/websocket/editor/editor_debugger_server_websocket.cpp`），**不用** `ai_bridge` 的 NetSocket 手写路径（其排坑 #2/#3 是 HTTP 场景特有）；
- **分派**：复用引擎内置 `JSONRPC` 类（`modules/jsonrpc`：`set_method`/`process_string`/标准错误码）做消息解析与分派，方法 handler 桥接 `SemanticRegistry`；`{ok,result}` ↔ 标准 result/error 的**映射在 C++ 分派层一次完成**（§5.1），Node 侧/前端不重复实现；Godot 侧不引第三方 JSON-RPC 库；
- **线程**：全主线程（SceneTree 帧泵，与 WebBridge/AiBridge 同线程，register_types 复用 `modules/ai` 的 EDITOR 级延迟启动）；
- **安全**：仅绑 127.0.0.1；握手令牌校验（`sidecar.hello` 首帧，不对即断开 + 日志告警）；连接数/消息大小上限（大消息如语义树快照按 WS 帧缓冲分片，参照 `ai_bridge` 每连接输出队列跨帧 flush 的成熟处理）；
- **排坑参照**：《AI-FIRST》§4 已踩坑清单——`Variant::is_null()` 不可信（用 `Dictionary::has()`）、`Main::cleanup` 清理顺序（帧泵判空 + `is_connected()`）、Vector 无 `push_front`、本 fork `Variant` 无 `convert()`；
- **事件源**：S1 先只做方法面；S2 把 `web_bridge.cpp` 的事件源（`selection_changed`/`scene_changed`/`undo_stack_changed` 等）重构为**多目标 fan-out**（`event_browser_id_` 单目标 → 注册表：CEF 浏览器 + sidecar 订阅者），sidecar 订阅经同一事件源，不重复接线。

---

## 6. SDK 侧：transport 抽象与服务命名空间（S2）

### 6.1 分层不变，底层多一个 transport

```
React 组件
  ↓ bridge.scene.* / editor.*（Godot 直连，不变）   services.*（sidecar 服务）
SDK 内部
  ↓ Transport 接口（invoke / onEvent / dispose）
      ├─ CefTransport（现有 CefViewClient 封装，默认）      ← 已落地
      └─ WsTransport（新：WS → sidecar，同信封）            ← S2
```

- `transport.ts` 抽 `Transport` 接口（现有实现改名 `CefTransport`）；`bridge.ts` 的方法绑定改为**可注入 transport**（默认 CEF）；新增 `services.ts` 命名空间（AI/工具链方法，绑定 `WsTransport`）；
- 类型化声明仍走 `registry.ts`（`defineMethod`/`defineEvent` 与 transport 解耦）；`@baize/rpc` 提供共享类型；
- React hooks（`useBridgeCall`/`useEditorEvent`）不变，新增 `useService`（同构封装）；
- **前端心智**：`bridge.scene.*` = 引擎权威操作（高频）；`services.*` = sidecar 服务（AI/工具链）——两通道显式区分，不混用。

### 6.2 WebBridge 增量方法

| 方法 | 说明 |
|---|---|
| `sidecar.get_info` | 返回 `{ available: bool, url?: string, token?: string }`（CEF 页面建立直连用；sidecar 未就绪时 available=false，页面轮询或订阅事件） |
| `sidecar.status_changed` 事件（可选） | sidecar 连接/断开状态推送（S2 事件多目标化的自然产物） |

---

## 7. 分阶段实施（每阶段独立可验收）

> 顺序原则：先打通进程外通道（S1），再直连 CEF（S2），再证明服务价值（S3），最后收发布运维（S4）。
> S0-S2 为"sidecar 三端直连落地"核心，一次可验收结果 = "三端两两直连 + 引擎命令经 sidecar 执行且 undo 一致"。

### S0：sidecar 工程骨架（`web/runtime` + `web/packages/rpc`）

- `pnpm-workspace.yaml` 增 `packages/*` 与 `runtime`；`web/packages/rpc` 纯类型包（信封 + 方法/事件 payload，零依赖）；
- `web/runtime`：`jsonrpc.ts`（编解码 + req_id 配对，与 sdk 同构）、`services/echo` 示例服务、CLI 入口（`tsx src/index.ts`）；
- 测试：Vitest（信封编解码、配对、超时、错误路径）+ Biome + `tsc --noEmit` 入 `pnpm -r run test/typecheck`；
- 构建：tsdown 多入口产物（`dist/index.js` 起）。

**验收**：`cd web && pnpm install && pnpm -r run test` 全绿；独立起 `tsx src/index.ts`，任一 WS 客户端（Node `ws`）调 `echo` 返回配对结果；`tsdown` 产出 dist 且 `node dist/index.js` 等价运行。

### S1：NodeJS↔Godot 通道（sidecar_server + spawn/生命周期 + 令牌）

- C++：`modules/ai/sidecar_server.{h,cpp}`（WS server + 帧泵 + 令牌握手 + 引擎内置 `JSONRPC` 类分派 → SemanticRegistry + 连接上限）；`register_types.cpp` 接线；
- spawn 管理：编辑器启动钩子（沿用 `ai_bridge` 的 MessageQueue 第一帧模式）→ `OS::create_process`（env 传 token/url/项目路径）；断开检测 + 自动重启（退避 + 上限）；退出钩子顺序（§4.4）；
- env：`BAIZE_SIDECAR=0|1|dev`（默认 1）、`BAIZE_GODOT_WS_PORT`（默认 47654）、`BAIZE_SIDECAR_PORT`（默认 47655）、`BAIZE_NODE`、`BAIZE_SIDECAR_TOKEN`（缺省自动生成）；
- 方法面：`sidecar.hello/health/echo` + SemanticRegistry 全量方法透传（`scene.get_node_count` / `scene.create_node` / `editor.get_state` / `editor.undo` 等）；
- 日志：sidecar stdout/stderr → Godot 日志文件（`user://logs/sidecar.log`）+ 编辑器退出时收尾（防残留句柄）。

**验收**（编辑器实机）：
1. 启动编辑器 → 日志见 sidecar 进程拉起 → WS 握手成功（hello 应答）；
2. 外部 `ws` 客户端（带 token）连 Godot WS → `scene.create_node("SidecarNode")` 返回、编辑器场景树出现节点、`editor.undo` 可撤销（undo 入栈与人工一致）；
3. 错误令牌 → 拒绝 + 明确日志；kill sidecar 进程 → 自动重启并在上限内恢复握手；
4. `BAIZE_SIDECAR=0` → 无 spawn、无告警噪声；端口被占 → 清晰报错不静默回退。

### S2：CEF↔NodeJS 直连 + 事件多目标化

- C++：`web_bridge` 事件源重构为多目标 fan-out（CEF 浏览器 + sidecar 订阅者）；`sidecar.get_info` 方法；`sidecar_ready` 事件链路（sidecar 上报 CEF 面 url/token）；
- sdk：`Transport` 接口 + `WsTransport` + `services` 命名空间；`@baize/rpc` 类型接入；
- ui：属性面板加 sidecar 状态指示器（get_info + 订阅），首个 `services.*` 消费（如 `services.echo` 或状态查询）——**证明页面直连 sidecar 且与 Godot 直连方法并存互不干扰**；
- sidecar：CEF 面 WS server（token 校验、回环绑定）+ `sidecar.get_info` 数据源。

**验收**：
1. 页面 `services.*` 调用到达 sidecar 并返回（与 `bridge.scene.*` 的 Godot 直连同时在线）；
2. Godot→sidecar 事件订阅生效：编辑器选中变化 → sidecar 收到 `editor.selection_changed` 通知（同一事件源，CEF 与 sidecar 都收到）；
3. 页面显式展示"sidecar 在线/离线"状态；kill sidecar → 页面状态离线 → 自动重启 → 恢复在线（事件重订阅完成）。

### S3：首个真实服务（**用户裁决选型**，§9）

| 候选 | 内容 | 验收形态 | 依赖 |
|---|---|---|---|
| **Agent 服务（推荐）** | LLM 会话 + SemanticRegistry 工具集（场景树读写/属性/undo/截图），自愈闭环（《设计》§5.7） | 面板对话："建一个带脚本的玩家节点" → agent 经 sidecar 通道调 `scene.create_node`/`editor.set_prop` → 编辑器出现节点、undo 可撤 | LLM API key / 网络（环境依赖） |
| LSP worker | tsgo/TypeScript 子进程 → Monaco 项目级检查 | Monaco 报项目级类型错误（超越内置 worker 的全量检查） | tsgo 可用性 |
| 资产管线 | 批量导入/元数据处理（如 `tools/easy_bonemap` 工具链服务化） | 批量导入任务在 sidecar 并行执行 + 进度事件 | 场景定义 |

### S4：发布与运维

- tsdown 产物按服务边界输出（agent-runtime.js 等）→ `bin/sidecar/`（stage 脚本扩展，仿 `stage_ui.py` 原子替换）；
- Node SEA 单文件打包落地（发布形态，用户零 Node 依赖——§3.1 已裁决）；`.nvmrc`/engines 固定；
- CI：`web` workspace 加 test/typecheck/lint 任务；类型漂移校验（C++ 消息表 ↔ `@baize/rpc`）立项；
- 退出顺序、崩溃重启上限、日志轮转收尾；性能基线（hello 往返延迟、大消息吞吐）留档。

**验收**：分发形态（非开发机）下 sidecar 以 **SEA 单文件**运行（用户零 Node 依赖，已裁决 §3.1）；编辑器退出零残留进程；双实例端口冲突报错清晰。

---

## 8. 安全与风险

### 8.1 安全

| 项 | 方案 |
|---|---|
| 绑定 | 两个新 WS server 均仅 127.0.0.1（Godot 面 + sidecar CEF 面） |
| 鉴权 | 单令牌/实例：spawn env → sidecar（Godot 面）；WebBridge `sidecar.get_info` → CEF 面；握手首帧校验，失败断开 |
| 日志 | token 不落日志/不写盘；env 仅 spawn 子进程可见 |
| 越权 | sidecar 只经 SemanticRegistry 调用（能力面 = 有限面，不做全 API 反射，沿用《设计》§4 桥 API 面原则）；文件系统仍走引擎 FileAccess（sidecar 不直接碰 FS） |

### 8.2 风险

| 风险 | 评估 | 缓解 |
|---|---|---|
| Node 运行时分发 | 用户机器无 Node → sidecar 不可用 | **发布形态 = SEA 单文件（用户零 Node 依赖，§3.1，已裁决）**；SEA 落地前（开发期/中间期）`BAIZE_SIDECAR=0` 可关 + Node 缺失明确报错 |
| WS 通道在本 fork 的可靠性 | WebSocketServer 路径（TCPServer+poll）已被编辑器 debug server 长期使用，风险低 | S1 先方法面、后事件面；压测大消息（语义树快照 ~780KB） |
| 事件风暴/订阅膨胀 | 订阅全量事件高频推送 | 订阅白名单 + 增量 diff（沿用现有帧轮询 diff 语义）；每订阅者可过滤 |
| sidecar 状态一致性 | 缓存引擎状态产生漂移 | 架构约束：不缓存权威状态，只发命令收事件（§2） |
| 进程树退出残留 | Win/mac 子进程清理不净 | 退出钩子顺序 + 进程组 kill + S4 实机验证零残留 |
| 双实例冲突 | 固定端口冲突 + CEF cache 独占（已知） | 沿用 `AI_BRIDGE` 端口报错口径；webview CEF cache 修复在途 |
| 能力面双份漂移 | sidecar 通道若自建方法表 → 漂移 | sidecar_server 一律走 `SemanticRegistry`（同 ai_bridge），不新增注册路径 |

---

## 9. 待决策点（用户）

| # | 决策点 | 选项 | 推荐 |
|---|---|---|---|
| 1 | S3 首个真实服务 | Agent 服务 / LSP worker / 资产管线 | **Agent 服务**：消费既有语义能力面、差异化最大、验收直观（但依赖 LLM API） |
| 2 | Node 分发策略 | 要求系统 Node / SEA 单文件打包 | **已裁决（2026-08-04）：发布 = SEA 单文件打包，用户零 Node 依赖；开发期用本机 Node 24.x（§3.1）** |
| 3 | sidecar 默认开启？ | 默认开（`BAIZE_SIDECAR` 缺省 1）/ 显式开启 | 默认开（沿 `AI_BRIDGE` 已裁决口径；`=0` 可关） |
| 4 | 目录形态 | `web/runtime`（并入现有 workspace）/ 根级 `runtime/` | **`web/runtime`**（单一 lockfile/toolchain，§3.2） |
| 5 | AI MCP 宿主迁移 | S3 随 Agent 服务同步迁（sidecar 内 Node MCP server，C++ `ai_bridge` 退役）/ 暂缓（保留 C++ `ai_bridge`） | **S3 同步迁**：`ai_bridge` 无实际使用，无兼容负担、无过渡期；C++ 手写 HTTP 的规范跟进成本换 Node MCP SDK（§5.2） |

---

## 10. 相关文档与证据

- 《TS-CEF嵌入与NodeSidecar-设计.md》（MiBlog）：§1.2 工具链裁决、§4 三端两两直连、§5.4 生命周期、§5.7 Agent 面板——本计划的上位裁决来源
- 《TS路线-WebUI架构-桥协议与前端SDK.md》（doc/plans）：CEF↔Godot 通道现状 + 协议语义（`{ok,result}`、req_id、点号命名空间）
- 《WebUI前端工程-实现文档-sdk与ui-workspace.md》：web/ workspace 现状、Node 24.14/24.18 实测、stage_ui 机制
- 《实施记录-AI-FIRST-P1-P2-语义接口与MCP.md》：SemanticRegistry/ai_bridge 实现、端口与鉴权模式、排坑清单（§4 编号直接引用）；ai_bridge 定位（实验品）与退役路径见 §1.1/§5.2
- 仓库事实：`modules/webview/web_bridge.h`（单目标事件源 `set_event_browser_id`）、`modules/ai/semantic_registry.h`（能力面注册表）、`modules/websocket/editor/editor_debugger_server_websocket.cpp`（WS server 参照）、`web/package.json`（workspace 现状）
