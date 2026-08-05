# Godot 编辑器 UI 重构（TS 路线）——Node Sidecar 落地计划

> **状态**：落地计划（2026-08-04；2026-08-05 依 shifu 审查修订——双令牌/进程监督前置/线级合同/资源预算/退出顺序/退役 gate 等，修订点均标注「审查修订」）。衔接《TS-CEF嵌入与NodeSidecar-设计.md》（§1.2 工具链 / §4 三端两两直连 / §5.4 生命周期）、
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
| CEF 接入（窗口模式/非 OSR：CEF 原生子窗口渲染、像素零回传、输入/IME 原生直收） | `modules/webview/`（webview_core / web_panel / editor_web_dock / cef_application_mac） | ✅ MVP1 完成，MVP2 实机验收 4 条进行中（《桥协议》§6）；渲染 = 非 OSR 窗口模式（最终态，`webview_core.cpp:1028` `windowless_rendering_enabled=0`、`:1149` `SetAsChild`） |
| CEF↔Godot 进程内桥（方法注册表 + req_id 配对 + 事件源 + 字体体系） | `modules/webview/web_bridge.{h,cpp}` | ✅ 方法 10 个、事件 6 个；事件源目前**单浏览器目标**（`set_event_browser_id`） |
| 前端 workspace（sdk + ui） | `web/`（@baize/ui-sdk + @baize/ui，React 19 + Vite 8 + Tailwind） | ✅ sdk 27 单测（静态计数：transport 11 + registry 16，以 CI 实跑为准；历史文档「13」已过期）；产物经 `misc/scripts/stage_ui.py` 进 `bin/webview/ui/` |
| 语义能力面（注册表 + 语义操作 + 语义 UI 树） | `modules/ai/semantic_registry.{h,cpp}` + `semantic_ops` + `editor_ui_tree` | ✅ 唯一事实源：方法名/描述/schema/handler 集中注册 |
| MCP 暴露层（HTTP server） | `modules/ai/ai_bridge.{h,cpp}` | ⚠️ **实验品，无实际使用**（用户确认 2026-08-04）：127.0.0.1:47653（`AI_BRIDGE_PORT`），可选 token 鉴权（`AI_BRIDGE_TOKEN` 未设时不强制），11 工具（ui.*/editor.*/scene.*）；定位 = 能力面活体参考实现，**S3 且 Node MCP 达成功能/鉴权等价后退役**（gate 见 §5.2/§9-5） |

### 1.2 缺口（sidecar 未落地）

- **无 sidecar 进程**：仓库无 `runtime/`、无 Node sidecar 代码、无 spawn/生命周期管理（grep 全仓库 `sidecar` 仅设计文档提及）；
- **无 NodeJS↔Godot 通道**：《设计》§4 裁决的 WebSocket/JSON-RPC 未实现；引擎能力目前只有两条暴露路径——CEF 进程内桥（`web_bridge`）与 MCP HTTP（`ai_bridge`），**无进程外双向通道**（MCP HTTP 无服务端推送，SSE 事件面暂缓）；
- **无 CEF↔NodeJS 通道**：页面只能调 Godot，不能直连 sidecar 服务（Agent/LSP/工具链）；
- **能力面未合流**：`web_bridge` 方法注册表与 `SemanticRegistry` 各自维护（`semantic_registry.h` 注释明示"WebBridge 委托列为后续"）。

### 1.3 本文交付边界

- **范围**：sidecar 工程落地（S0）→ NodeJS↔Godot 通道（S1）→ CEF↔NodeJS 直连 + 事件多目标化（S2）→ 首个真实服务（S3）→ 发布运维（S4）。S0-S2 构成"sidecar 三端直连落地"核心；S3 服务选型留用户裁决（§9）；
- **不涉及**：不改 Godot 状态权威模型（场景树/属性/UndoRedo 仍在引擎）；不迁移全部 WebBridge 方法（S1 仅委托 4 个重叠方法到 SemanticRegistry，其余 S2 评估迁移或明确为 WebView host 能力，见 §5.2）；不涉及渲染方案——WebDock 渲染已定型为非 OSR 窗口模式（CEF 原生子窗口、像素零回传、输入/IME 原生直收），OSR 时代的 GPU 直通 / fork CEF 等计划已随路线演进废弃，归档于 `页面渲染选型-OSR与非OSR/`（§10）。

---

## 2. 目标与边界

**一句话**：在编辑器进程内补一条 NodeJS sidecar 通道（Godot 为 WS server + spawn 管主，sidecar 为进程外服务宿主），
实现《设计》§4 的"三者两两直连"拓扑，并以 Agent/LSP/资产管线等服务证明进程边界价值。

| 项 | 结论 |
|---|---|
| sidecar 职责 | 工具链/插件运行时：Agent 服务、LSP 查询、资产管线、npm 生态工具宿主（《设计》§1.2/§5.7） |
| 状态边界 | **不缓存引擎状态**（《设计》§4 约束 1）；sidecar 只发命令、收事件，Godot 唯一权威 |
| 连接拓扑 | 两两直连：CEF↔Godot 进程内桥（已落地）；NodeJS↔Godot WS/JSON-RPC（**本文 S1**）；CEF↔NodeJS WS（**本文 S2**） |
| 生命周期 | Godot 管 spawn/重启/退出（《设计》§5.4）；开发期支持外部 `tsx watch` 自管（HMR），Godot listen、外部 sidecar 主动连接（审查修订 P1-5） |
| 首个服务 | 候选：Agent 服务（推荐，§7 S3）/ LSP worker / 资产管线——**用户裁决** |

**为什么不做"CEF→NodeJS→Godot 链式"**：沿用《设计》§4 裁决——核心 Selection/Property 交互绕 NodeJS 两跳且引入一致性风险；NodeJS 只有自己的两条边。

---

## 3. 技术栈与目录裁决

### 3.1 技术栈（沿《设计》§1.2 裁决，落地时实测版本）

| 层 | 选择 | 说明 |
|---|---|---|
| Runtime | **Node.js 26 LTS（目标）** | 《设计》已裁决。实测 Win 24.14.0（《sdk与ui-workspace》§1）；`engines: ">=24"` 与 `.nvmrc` 为 **S0 交付**（当前不存在）；目标 26 LTS 及版本矩阵实施时锁定并验证 |
| TS 运行（开发） | **tsx**（watch 模式 = sidecar HMR） | 用户裁决（2026-08-05）：后端 dev = tsx、build = tsdown。sdk/ui 现用 Vite（前端构建器，不适用后端服务进程）；sidecar 新增 tsx/tsdown，复用同一 pnpm/TS/Vitest/Biome 体系（审查修订 P2-2） |
| 构建 | **tsdown** | 开发/中间态按服务边界多产物（agent-runtime.js / asset-worker.js / lsp-worker.js）；发布态每服务先 bundle 为单一 JS（SEA 注入要求），两阶段产物分目录（审查修订 P1-9） |
| 打包（发布形态） | **Node SEA 单文件** | **用户裁决（2026-08-04）：发布不要求用户有 Node 运行时**——每服务一个 SEA 自包含可执行（Node 20+ 官方 Single Executable Applications）：`ws`/`vscode-jsonrpc`/`pino`/`zod` bundle 进注入 JS，不依赖外部 node_modules；napi-rs native addon 与 SEA 组合成熟度落地实测（列为风险）；按 Windows x64 / macOS arm64/x64 分平台构建、签名/公证（S4）；开发期用本机 Node 24.x |
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
    │   ├── jsonrpc.ts              ← vscode-jsonrpc 适配 + 方法分派 + {ok,result} 映射（信封类型共享 @baize/rpc）
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
│     WS server（port 0，新）                 │（已落地）          │
└────────────┼──────────────────────────────────┼───────────────────┘
             │ spawn env 传 godot token/url/项目路径 │
             │ (sidecar 主动连回 Godot)           │
    ┌────────┴──────────────────┐      ┌─────────┴──────────┐
    │ Node sidecar（web/runtime）│◄────►│ CEF 页面（web/ui）  │
    │ Agent/LSP/资产服务宿主      │  WS  │ sdk services.*    │
    │ WS server（CEF 面）        │ 直连  │ （S2，新 transport）│
    └───────────────────────────┘      └────────────────────┘
```

> 注：`ai_bridge`（MCP HTTP）为实验品（用户确认 2026-08-04，无实际使用），S3 选 Agent 且 Node MCP 达成功能/鉴权等价后退役（gate 见 §5.2/§9-5）——终态不含它。

### 4.2 角色与通道

| 通道 | 方向/角色 | 传输 | 状态 |
|---|---|---|---|
| CEF↔Godot | 编辑器核心交互（高频低延迟） | CefViewClient.invoke + TriggerEvent（进程内） | ✅ 已落地 |
| **NodeJS↔Godot** | 引擎能力调用（批量导入/场景操作/agent 命令）+ 事件订阅 | **WS/JSON-RPC**（Godot = server，sidecar = client，S1 新建） | ⬜ S1 |
| **CEF↔NodeJS** | UI 调服务（AI 面板/工具链/LSP） | **WS**（sidecar = server，CEF = client，S2 新建） | ⬜ S2 |

**角色裁决**：Godot = 状态权威 + 生命周期管主（spawn/重启/退出）；sidecar = 服务宿主（无权威状态）；
CEF = 表现层。连接建立后数据面直连（VS Code main-process 模式，《设计》§4）。

### 4.3 握手与令牌链（双令牌/编辑器实例，审查修订 P0-1/P2-1）

```
1. Godot 启动 → 生成 godot_sidecar_token（随机 32B hex，仅存内存）；Godot 面 WS listen port 0（OS 分配）
2. Godot spawn sidecar：env 传 BAIZE_GODOT_WS_URL（ws://127.0.0.1:<实际端口>）、BAIZE_GODOT_TOKEN、
   BAIZE_PROJECT_PATH；sidecar 连 Godot WS → 首帧 auth：{ method:"sidecar.hello", token }（认证 deadline 3s）
3. sidecar 起 CEF 面 WS server（port 0）→ 生成 cef_service_token → 经 Godot 事件 sidecar_ready 上报
   { cef_ws_url, cef_ws_token }
4. CEF 页面经 WebBridge 新方法 sidecar.get_info 取 { url, token }（仅 main frame 可调）→ sdk 建立直连
```

- **双令牌理由（审查修订）**：CEF 页面与 sidecar 信任级别不同——`godot_sidecar_token` 仅授予被 Godot spawn 的 sidecar（可调 SemanticRegistry 全量方法）；`cef_service_token` 仅允许访问 sidecar `services.*`，**不能连接 Godot WS**。原单令牌设计会形成“页面 → sidecar → Godot 全能力面”的提权路径（原 P0-1，降级为 P1 加固项）；
- **CEF 导航边界（审查修订）**：WebBridge 调用仅接受 bundled `file:///…/webview/ui/` 主 frame（main frame 校验，导航离开即撤销桥与 token）；CEF 面 WS 校验 `Origin` 与预期页面身份；iframe/错误 Origin/远程导航一律拿不到 `get_info`；
- 令牌不落磁盘、不进日志；spawn 模式 port 0 无端口冲突，dev/外部调试模式才允许固定端口覆盖（此为明确策略，非 bind 失败后的静默回退）；bind 失败仍清晰报错（显示端口 + 来源 + 改法），**不静默回退、不自动换端口**。

### 4.4 生命周期（《设计》§5.4 落地）

| 项 | 方案 |
|---|---|
| 进程监督（S1 前置） | 仓库级 `ProcessSupervisor`（扩展 OS API）：原子接收 env + cwd + stdout/stderr pipes + 进程树 ownership；Windows = Job Object（关闭即 kill tree），macOS/Unix = 新 session/进程组 + killpg + waitpid。**不得**经修改 Godot 全局环境注入，**不得**经 argv 传 token（当前 `OS::create_process` 无 per-spawn env/stdio/进程树能力，`core/os/os.h:216-220`、`platform/windows/os_windows.cpp:1554-1620`——审查修订 P1-1） |
| 启动 | 编辑器（editor 模式）启动时经 ProcessSupervisor spawn（懒加载备选：首次 sidecar 服务被调用时，S4 后评估）；`BAIZE_SIDECAR=0` 关闭、`BAIZE_SIDECAR=dev` 外部自管（Godot listen，外部 sidecar 主动连接；父环境须显式提供同一 `BAIZE_SIDECAR_TOKEN`，缺失则拒绝 dev 模式并给出启动指令——审查修订 P1-5） |
| 连接 | 握手退避序列 0.5/1/2/4/8s（单次封顶），上限 10 次；sidecar 侧认证 deadline 3s；超时告警不阻塞编辑器（sidecar 是增强不是命门） |
| 崩溃恢复 | Godot 检测 WS 断开 → 若进程仍活着先杀 → 自动重启（同退避序列）；单次会话上限 3 次，**稳定运行 5 分钟后重置计数器**，第 4 次崩溃不再自动重启（明确日志 + 用户手动恢复，审查修订 P2-7）；重启后重新握手 + 事件重订阅；**epoch/generation 递增**，旧连接的 response/事件不得串入新连接 |
| 退出 | 唯一 owner = 编辑器 pre-exit 路径（EditorNode 仍存活时显式编排，不依赖模块级 teardown 顺序——`Main::cleanup` 实际顺序为 SceneTree → EDITOR 模块 → SCENE 模块：AI 在 EDITOR 级、CEF 在 SCENE 级，审查修订 P1-6）：UI 面板停发事件 → sidecar（发 shutdown 通知 + 等 2s + kill 进程树含派生 worker）→ CEF 随 SCENE 级自然 shutdown → 引擎；无残留进程（Win Job Object / mac 进程组） |
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
- 选标准实现（而非自定义 `{ok,result}` 信封）的原因：`vscode-jsonrpc`（Node）消费标准 JSON-RPC 2.0 线格式、LSP 同源；C++ 侧解析实现裁决见 §5.3（内置 `JSONRPC` 类缺 error.data/string id 支持，由 sidecar_server 自做严格解析，不重复实现映射）；
- **线级合同（审查修订 P1-3）**：每个 WS text message = 恰好一个 JSON-RPC document；request `id` 一律 string（SDK 生成）；Godot 侧不发 request，**拒绝 response 输入**；通知仅允许白名单方向（Godot→sidecar 事件下行、sidecar→CEF 服务进度）；**batch 请求显式拒绝（-32600）**；若 Node 侧使用 `vscode-jsonrpc`，必须自定义 `MessageReader/Writer` 适配 WS frame（不能把 Content-Length stream 当 WS payload）；
- **错误码映射（审查修订 P1-2）**：未知方法 -32601、参数校验 -32602、业务失败统一 -32000（JSON-RPC server error range），内部字符串码放 `error.data.code`——不得把字符串内部码直接塞进数值 `error.code`；
- 消息类型：`RpcRequest/RpcResponse/RpcError/RpcNotification` + 方法/事件 payload 类型，全部在 `web/packages/rpc` 一处声明；sdk 与 runtime 从包导入；C++ 侧（sidecar_server）手写对齐，**协议一致性向量（同一 JSON fixture 同时跑 C++ 与 TS）为 S1 merge gate**（原“类型漂移校验 CI 后续”从 S4 立项提前，审查修订 P2-6）。

### 5.2 能力面：SemanticRegistry 为唯一事实源

`sidecar_server`（C++，新，位于 `modules/ai/`）**不分发到 WebBridge**，而是直接查询 `SemanticRegistry`（方法元数据 + `validate_args` + handler）——与 `ai_bridge` 的 MCP 工具面同源：

| 消息 | 语义 | 分发 |
|---|---|---|
| `scene.*` / `editor.*` / `ui.*`（SemanticRegistry 方法） | 引擎能力调用 | `SemanticRegistry::find` + `validate_args` + handler（与 MCP tools/call 完全一致） |
| `sidecar.hello` / `sidecar.health` | 握手/健康检查 | sidecar_server 自身 |
| `sidecar.subscribe` / `unsubscribe`（事件名列表） | 事件订阅 | sidecar_server 维护订阅表（S2 事件多目标化后生效） |
| `editor.selection_changed` 等（通知） | Godot→sidecar 事件下行 | 事件源 fan-out（S2） |

- **事件订阅契约（审查修订）**：S2 定义订阅白名单（事件名列表，来源 = `@baize/rpc` 事件声明 + C++ 事件源注册表，双端一致）、每事件 payload schema、顺序/去重/coalesce 规则（状态型事件可合并，订阅者可过滤）；“不缓存权威状态”不等于不能维护连接级游标（重订阅快照语义 S2 细化）。

**`ai_bridge` 定位与退役路径（gate 修订，审查修订 P2-5）**：`ai_bridge` 是实验品（用户确认 2026-08-04，无任何实际使用）——其价值是能力面的**活体参考实现**：验证 SemanticRegistry 语义 + 排坑清单（《AI-FIRST》§4 已归档，删代码不丢知识）。S1 的 WS 通道可用它交叉验证（同一能力面、两种传输、结果应一致）。**退役 gate**：仅当 S3 选 Agent 服务且 sidecar 内 Node MCP server（streamable HTTP）完成 tools/list、tools/call、鉴权与外部消费者迁移验收后，才删除 `ai_bridge`；删除清单**包含 `register_types.cpp`/config/build 接线**，不限于 `{h,cpp}`；若 S3 选 LSP/资产管线则保留到后续阶段。`modules/ai` 终态收敛为 SemanticRegistry + semantic_ops + editor_ui_tree + sidecar_server。不新增第二份能力实现：sidecar_server 直接查询 `SemanticRegistry`（与 ai_bridge 的 MCP 工具面同源，方法实现只写一份）。

### 5.3 C++ 实现要点（`modules/ai/sidecar_server.{h,cpp}`）

- **传输**：复用 `modules/websocket`（引擎内置 wslay）：`TCPServer::listen` + `WebSocketPeer::accept_stream` + 每帧 `poll`——模式参照已在本 fork 编辑器验证的 `EditorDebuggerServerWebSocket`（`modules/websocket/editor/editor_debugger_server_websocket.cpp`），**不用** `ai_bridge` 的 NetSocket 手写路径（其排坑 #2/#3 是 HTTP 场景特有）；参照实现仅管理单个 pending peer（含 3s 握手超时），多 peer 管理/背压需自建；
- **资源预算（审查修订 P1-4）**：`accept_stream` 前显式设置 inbound/outbound buffer（提高有界 message 上限——wslay 的 `max_recv_msg_length` 在握手时固定为 inbound size，默认 65535B，普通 WS 分片不能绕过重组后长度限制；语义树快照 ~780KB，buffer 以 4 MiB 起步实测）；outbound 单条超限会 `ERR_OUT_OF_MEMORY`（`modules/websocket/wsl_peer.cpp:776-778`），若超限仍发生则启用应用层 chunk 协议（transfer_id/index/count）；每 peer 认证 deadline 3s、连接数上限、每帧消息/字节预算、outbound high-water mark、慢客户端以 1009/策略码关闭；
- **分派（审查修订 P1-2）**：**裁决 = sidecar_server 自做严格 JSON-RPC 2.0 解析/分派**（薄层，遵守规范语义：error 携带 data、string id、通知语义），方法 handler 桥接 `SemanticRegistry`。**不依赖**引擎内置 `JSONRPC` 类（`modules/jsonrpc`）——其 handler 返回值无条件进 `result`、`make_response_error` 不支持 `error.data`、response 路径只收数值 id，且非 virtual 扩展点（`modules/jsonrpc/jsonrpc.cpp:142-181,60-71,185-201`），无法完成 §5.1 要求的映射；备选 = 扩展 `modules/jsonrpc`（影响引擎内置模块，不取）。`{ok,result}` ↔ 标准 result/error 的**映射在 C++ 分派层一次完成**（§5.1），Node 侧/前端不重复实现；Godot 侧不引第三方 JSON-RPC 库；
- **线程**：全主线程（SceneTree 帧泵，与 WebBridge/AiBridge 同线程，register_types 复用 `modules/ai` 的 EDITOR 级延迟启动）；
- **安全（审查修订 P0-1）**：仅绑 127.0.0.1；**双令牌校验**（`godot_sidecar_token` 校验 Godot 面首帧，`cef_service_token` 校验 CEF 面，两令牌互不通用，见 §4.3）；连接数/消息大小上限（见资源预算，替代原“按 WS 帧缓冲分片”表述）；
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
- **WsTransport 状态机（审查修订 P2-3）**：`idle/connecting/open/closed/disposed`；断线立即以稳定错误码拒绝全部 pending；自动重试仅限只读/幂等方法（有副作用方法不自动重试）；重连必须重新 `sidecar.get_info`、校验实例 epoch、恢复订阅；`dispose` 移除 listener、清 timer、拒绝 pending；每个状态转换配 Vitest；
- 类型化声明仍走 `registry.ts`（`defineMethod`/`defineEvent` 与 transport 解耦）；`@baize/rpc` 提供共享类型；
- React hooks（`useBridgeCall`/`useEditorEvent`）不变，新增 `useService`（同构封装）；
- **前端心智**：`bridge.scene.*` = 引擎权威操作（高频）；`services.*` = sidecar 服务（AI/工具链）——两通道显式区分，不混用。

### 6.2 WebBridge 增量方法

| 方法 | 说明 |
|---|---|
| `sidecar.get_info` | 返回 `{ available: bool, url?: string, token?: string }`（CEF 面 WS url + `cef_service_token`；**仅 main frame 可调**，见 §4.3；sidecar 未就绪时 available=false，页面轮询或订阅事件） |
| `sidecar.status_changed` 事件（**必选**） | sidecar 连接/断开状态推送（S2 事件多目标化的自然产物；S2 验收要求页面即时在线/离线状态，不依赖轮询） |

---

## 7. 分阶段实施（每阶段独立可验收）

> 顺序原则：先打通进程外通道（S1），再直连 CEF（S2），再证明服务价值（S3），最后收发布运维（S4）。
> S0-S2 为"sidecar 三端直连落地"核心，一次可验收结果 = "三端两两直连 + 引擎命令经 sidecar 执行且 undo 一致"。

### S0：sidecar 工程骨架（`web/runtime` + `web/packages/rpc`）

- `pnpm-workspace.yaml` 增 `packages/*` 与 `runtime`；`web/packages/rpc` 纯类型包（信封 + 方法/事件 payload，零依赖）；
- `web/runtime`：`jsonrpc.ts`（编解码 + req_id 配对，与 sdk 同构）、`services/echo` 示例服务、CLI 入口（`tsx src/index.ts`）；
- 测试：Vitest（信封编解码、配对、超时、错误路径）+ Biome + `tsc --noEmit` 入 `pnpm -r run test/typecheck`；
- 构建：tsdown 多入口产物（`dist/index.js` 起）。

**验收**（审查修订）：
1. `pnpm -r run test`（Vitest）+ `tsc --noEmit` + Biome check + `tsdown` build 全绿（四个命令分别列出、不等价）；
2. echo 服务经**测试专用** `127.0.0.1:0` WS 或 in-memory/stdio 验证（不提前实现生产 CEF server）；任一 WS 客户端调 `echo` 返回配对结果；`node dist/index.js` 等价运行；
3. `@baize/rpc` 零运行时依赖、纯类型（无生成 JS）、package exports 可检查；
4. engines/`.nvmrc`、tsx/tsdown 版本锁定完成；
5. 协议向量（固定 fixture：编解码/超时/错误/notification/batch 策略）通过，不只测 req_id happy path。

### S1：NodeJS↔Godot 通道（sidecar_server + spawn/生命周期 + 令牌）

- C++：`modules/ai/sidecar_server.{h,cpp}`（WS server + 帧泵 + 令牌握手 + **自做严格 JSON-RPC 2.0 解析/分派**（§5.3 裁决，不依赖引擎内置 `JSONRPC` 类）→ SemanticRegistry + 连接上限）；`register_types.cpp` 接线；
- spawn 管理：编辑器启动钩子（沿用 `ai_bridge` 的 MessageQueue 第一帧模式）→ **ProcessSupervisor**（env 传 godot token/url/项目路径，§4.4）；断开检测 + 自动重启（退避 + 上限）；退出编排（§4.4）；
- env：`BAIZE_SIDECAR=0|1|dev`（默认 1）、`BAIZE_GODOT_WS_URL`（port 0 实际端口派生，spawn 时下发）、`BAIZE_GODOT_TOKEN`（spawn 时自动生成）、`BAIZE_PROJECT_PATH`、`BAIZE_NODE`、`BAIZE_SIDECAR_TOKEN`（仅 dev 模式：父环境显式提供，缺失拒绝 dev 模式——审查修订 P1-5）；
- 方法面：`sidecar.hello/health/echo` + SemanticRegistry 全量方法透传（`scene.get_node_count` / `scene.create_node` / `editor.get_state` / `editor.undo` 等）；
- 日志：sidecar stdout/stderr → Godot 日志文件（`user://logs/sidecar.log`，**有界 + token redaction，S1 起**）+ 编辑器退出时收尾（防残留句柄）。

**验收**（编辑器实机，审查修订）：
1. 启动编辑器 → 日志见 sidecar 进程拉起 → WS 握手成功（hello 应答）；
2. 外部 `ws` 客户端（带 token，**在禁用内置 sidecar 的会话中**）连 Godot WS → `scene.create_node("SidecarNode")` 返回、编辑器场景树出现节点、`editor.undo` 可撤销（undo 入栈与人工一致）；
3. **ProcessSupervisor 跨平台验收**：私有 env 注入、stdout/stderr 捕获、子进程再派生 worker 后的整树 kill + 句柄回收（reap）、token 不出现在 argv/日志；
4. **JSON-RPC 一致性向量**：标准错误码 + data、string id、通知无响应、非法 request（-32600）、batch 拒绝、UTF-8、迟到/重复 id；
5. **WS 资源限制**：认证 deadline 3s、连接数上限、~780KB 快照策略、慢客户端关闭、每帧预算；
6. 错误令牌 → 拒绝 + 明确日志；kill sidecar → 自动重启（退避序列），第 4 次崩溃不再自动重启；`BAIZE_SIDECAR=dev` 外部 watcher 正常连接（显式 token）、`BAIZE_NODE` 缺失明确报错；
7. **4 个重叠 WebBridge 方法委托后回归**（`scene.get_node_count`/`scene.create_node`/`editor.undo`/`editor.redo` 与前端既有调用一致）；
8. `BAIZE_SIDECAR=0` → 无 spawn、无告警噪声；端口冲突/权限 → 清晰报错不静默回退；正常退出与各竞态（握手中/重启退避中/CEF 关闭中）零残留进程；日志有界 + token 字段 redaction 生效。

### S2：CEF↔NodeJS 直连 + 事件多目标化

- C++：`web_bridge` 事件源重构为多目标 fan-out（CEF 浏览器 + sidecar 订阅者）；`sidecar.get_info` 方法；`sidecar_ready` 事件链路（sidecar 上报 CEF 面 url/token）；
- sdk：`Transport` 接口 + `WsTransport` + `services` 命名空间；`@baize/rpc` 类型接入；
- ui：属性面板加 sidecar 状态指示器（get_info + 订阅），首个 `services.*` 消费（如 `services.echo` 或状态查询）——**证明页面直连 sidecar 且与 Godot 直连方法并存互不干扰**；
- sidecar：CEF 面 WS server（token 校验、回环绑定）+ `sidecar.get_info` 数据源。

**验收**（审查修订）：
1. 页面 `services.*` 调用到达 sidecar 并返回（与 `bridge.scene.*` 的 Godot 直连同时在线）；`sidecar.status_changed` 驱动在线/离线状态；
2. **令牌隔离**：CEF 面 `cef_service_token` 不能连接 Godot WS；错误 Origin / iframe / 远程导航均拿不到 `get_info`（main frame 校验生效）；
3. Godot→sidecar 事件订阅生效：选中变化 → sidecar 收到 `editor.selection_changed`（同一事件源 fan-out，CEF 与 sidecar 都收到；一个慢/断开订阅者不阻塞其他订阅者）；订阅白名单、unsubscribe、重复订阅、事件顺序/coalesce 规则生效；
4. **WsTransport 恢复语义**：kill sidecar → pending 确定性失败 → 页面离线 → 自动重启 → 重新 `get_info` + epoch 校验 + 订阅恢复 → 在线；
5. CEF 既有 10 方法/6 事件回归通过。

### S3：首个真实服务（**用户裁决选型**，§9）

| 候选 | 内容 | 验收形态 | 依赖 |
|---|---|---|---|
| **Agent 服务（推荐）** | LLM 会话 + SemanticRegistry 工具集（场景树读写/属性/undo/截图），自愈闭环（《设计》§5.7） | 面板对话："建一个带脚本的玩家节点" → agent 经 sidecar 通道调 `scene.create_node`/`editor.set_prop` → 编辑器出现节点、undo 可撤 | LLM API key / 网络（环境依赖） |
| LSP worker | tsgo/TypeScript 子进程 → Monaco 项目级检查 | Monaco 报项目级类型错误（超越内置 worker 的全量检查） | tsgo 可用性 |
| 资产管线 | 批量导入/元数据处理（如 `tools/easy_bonemap` 工具链服务化） | 批量导入任务在 sidecar 并行执行 + 进度事件 | 场景定义 |

**验收**（审查修订）：进入 S3 前先选定唯一服务并写固定 fixture（输入、期望输出、undo/取消/失败路径、性能边界），三个候选不共享同一验收；若选 Agent/MCP，ai_bridge 退役 gate 见 §5.2（Node MCP tools/list + tools/call + 鉴权 + 外部消费者迁移验收后才可删除）。

### S4：发布与运维

- tsdown 产物按服务边界输出（agent-runtime.js 等）→ `bin/sidecar/`（stage 脚本扩展，仿 `stage_ui.py` 原子替换）；
- Node SEA 单文件打包落地（发布形态，用户零 Node 依赖——§3.1 已裁决）；`.nvmrc`/engines 固定；
- CI：`web` workspace 加 test/typecheck/lint 任务；类型漂移校验（C++ 消息表 ↔ `@baize/rpc`）为 **merge gate**（S1 起，§5.1）；
- 发布环境复验：退出顺序、崩溃重启上限、日志有界/轮转收尾；性能基线（hello 往返延迟、大消息吞吐）留档。

**验收**（审查修订）：分发形态（非开发机）下 sidecar 以 **SEA 单文件**运行（用户零 Node 依赖，已裁决 §3.1）且不依赖外部 node_modules；Windows x64 / macOS arm64/x64 分别构建、签名/公证、可执行权限、stage 目录清单与 hash 校验；PATH 无 Node 环境完成一次真实服务调用；编辑器退出（含 sidecar 派生 worker）零残留进程 + 限定最长退出时间；双实例（dev 固定端口模式）冲突报错清晰；hello 往返延迟、780KB 传输、主线程单帧耗时、RSS、日志大小给出数值阈值；类型漂移 CI 为实际 merge gate（非“立项”）；stage 验证原子替换、旧产物清除与失败回滚。

---

## 8. 安全与风险

### 8.1 安全

| 项 | 方案 |
|---|---|
| 绑定 | 两个新 WS server 均仅 127.0.0.1（Godot 面 + sidecar CEF 面）；spawn 模式 port 0 动态分配，dev 模式才允许固定端口覆盖（审查修订 P2-1） |
| 鉴权 | **双令牌**（审查修订 P0-1）：`godot_sidecar_token` 经 spawn env 仅授予 sidecar（Godot 面首帧校验，认证 deadline 3s）；`cef_service_token` 经 `sidecar_ready` 事件 + WebBridge `sidecar.get_info` 分发，仅能访问 sidecar `services.*`，**不能连接 Godot WS**；握手失败断开 + 日志告警 |
| 主体隔离 | 三种 principal 权限区分（审查修订）：sidecar（Godot 面全量 SemanticRegistry）/ CEF 页面（仅 `services.*`）/ 外部调试客户端（仅 dev 模式，显式开启） |
| CEF 导航边界 | 桥调用仅接受 bundled `file://` 主 frame（main frame 校验）；导航离开撤销桥与 token；CEF 面 WS 校验 Origin；iframe/远程导航拒绝（审查修订） |
| 日志 | token 不落日志/不写盘；env 仅 spawn 子进程可见；日志有界（大小上限 + 保留数）+ token 字段 redaction，**S1 起生效**（审查修订 P1-10） |
| 越权 | sidecar 只经 SemanticRegistry 调用（能力面 = 有限面，不做全 API 反射，沿用《设计》§4 桥 API 面原则）；文件系统 = **项目根内受控只读 + 显式受控写**（路径 canonicalize、禁止越界/symlink escape），引擎权威状态写仍走 Godot/UndoRedo（审查修订 P1-8：LSP/npm/资产工具需直接访问项目文件，“绝不碰 FS”与职责冲突，S3 候选细化） |

### 8.2 风险

| 风险 | 评估 | 缓解 |
|---|---|---|
| Node 运行时分发 | 用户机器无 Node → sidecar 不可用 | **发布形态 = SEA 单文件（用户零 Node 依赖，§3.1，已裁决）**；SEA 落地前（开发期/中间期）`BAIZE_SIDECAR=0` 可关 + Node 缺失明确报错 |
| WS 通道在本 fork 的可靠性 | WebSocketServer 路径（TCPServer+poll）已被编辑器 debug server 长期使用，风险低；但参照实现仅管理单 pending peer | S1 先方法面、后事件面；按 §5.3 资源预算压测大消息（语义树快照 ~780KB）与多 peer |
| 事件风暴/订阅膨胀 | 订阅全量事件高频推送 | 订阅白名单 + 增量 diff（沿用现有帧轮询 diff 语义）；每订阅者可过滤；状态型事件 coalesce（§5.2） |
| sidecar 状态一致性 | 缓存引擎状态产生漂移 | 架构约束：不缓存权威状态，只发命令收事件（§2）；连接级游标不视为权威状态 |
| 进程树退出残留 | Win/mac 子进程清理不净 | ProcessSupervisor（Job Object / 进程组）+ 退出编排 owner（§4.4）+ S1 起实机验证零残留 |
| 双实例冲突 | dev 固定端口模式冲突 | spawn 模式 port 0 无冲突（审查修订 P2-1）；CEF cache 已按实例槽位隔离（`modules/webview/webview_core.cpp:986-1006`，原“修复在途”已过期）；dev 固定端口沿用 `AI_BRIDGE` 报错口径 |
| 能力面双份漂移 | sidecar 通道若自建方法表 → 漂移 | sidecar_server 一律走 `SemanticRegistry`（同 ai_bridge），不新增注册路径；S1 委托 4 个重叠 WebBridge 方法；类型漂移校验 = S1 merge gate（审查修订 P1-7） |

---

## 9. 待决策点（用户）

| # | 决策点 | 选项 | 推荐 |
|---|---|---|---|
| 1 | S3 首个真实服务 | Agent 服务 / LSP worker / 资产管线 | **Agent 服务**：消费既有语义能力面、差异化最大、验收直观（但依赖 LLM API） |
| 2 | Node 分发策略 | 要求系统 Node / SEA 单文件打包 | **已裁决（2026-08-04）：发布 = SEA 单文件打包，用户零 Node 依赖；开发期用本机 Node 24.x（§3.1）** |
| 3 | sidecar 默认开启？ | 默认开（`BAIZE_SIDECAR` 缺省 1）/ 显式开启 | 默认开（沿 `AI_BRIDGE` 已裁决口径；`=0` 可关） |
| 4 | 目录形态 | `web/runtime`（并入现有 workspace）/ 根级 `runtime/` | **`web/runtime`**（单一 lockfile/toolchain，§3.2） |
| 5 | AI MCP 宿主迁移 | S3 选 Agent 且 Node MCP 达成功能/鉴权等价后迁（含外部消费者盘点，harness 曾依赖 47653）/ 暂缓（保留） | **S3 同步迁（gate 化，审查修订 P2-5）**：退役 gate 见 §5.2——Node MCP 完成 tools/list + tools/call + 鉴权 + 外部消费者迁移验收后才删 `ai_bridge`（删除清单含 register_types/build 接线）；S3 选 LSP/资产则保留到后续 |
| 6 | C++ 侧 JSON-RPC 解析实现 | sidecar_server 自做严格解析（薄层，推荐）/ 扩展引擎内置 `modules/jsonrpc` | **自做薄层**：内置类缺 error.data/string id 且非 virtual 扩展点，改动隔离在 sidecar_server、协议向量可测（§5.3，审查修订 P1-2） |

---

## 10. 相关文档与证据

- 《TS-CEF嵌入与NodeSidecar-设计.md》（MiBlog）：§1.2 工具链裁决、§4 三端两两直连、§5.4 生命周期、§5.7 Agent 面板——本计划的上位裁决来源（**不在仓内，其内容无法仓内核实——证据缺口，审查标注**）
- 《TS路线-WebUI架构-桥协议与前端SDK.md》（doc/plans）：CEF↔Godot 通道现状 + 协议语义（`{ok,result}`、req_id、点号命名空间）
- 《WebUI前端工程-实现文档-sdk与ui-workspace.md》：web/ workspace 现状、Node 24.14 实测、stage_ui 机制
- 《实施记录-AI-FIRST-P1-P2-语义接口与MCP.md》：SemanticRegistry/ai_bridge 实现、端口与鉴权模式、排坑清单（§4 编号直接引用）；ai_bridge 定位（实验品）与退役路径见 §1.1/§5.2
- 《页面渲染选型-OSR与非OSR/》（doc/plans 子目录）：WebDock 渲染路线选型与落地记录——最终态 = 非 OSR 窗口渲染（《实施记录-WebDock非OSR窗口模式-MVP落地.md》《技术详解-WebDock原生子窗口-非OSR可行性分析与取舍.md》）；OSR 相关技术详解/实施计划归档于此，本文按最终态口径描述，不重复渲染细节
- 仓库事实：`modules/webview/web_bridge.h`（单目标事件源 `set_event_browser_id`）、`modules/ai/semantic_registry.h`（能力面注册表）、`modules/websocket/editor/editor_debugger_server_websocket.cpp`（WS server 参照，仅单 pending peer）、`modules/jsonrpc/jsonrpc.{h,cpp}`（内置 JSON-RPC 类限制：无 error.data/仅数值 id/非 virtual，§5.3）、`core/os/os.h:216-220` + `platform/windows/os_windows.cpp:1554-1620`（`create_process` 无 per-spawn env/stdio/进程树，§4.4）、`modules/webview/webview_core.cpp:986-1006`（CEF cache 实例槽位隔离已落地）、`doc/plans/实施记录-AI-FIRST-P1-P2-语义接口与MCP.md:126-150`（harness 曾依赖 47653——ai_bridge 退役前须盘点外部消费者）、`web/package.json`（workspace 现状：sdk/ui 用 Vite，无 tsx/tsdown）
- 2026-08-05 shifu 只读审查为本次修订依据（未改代码）；审查发现已按 P0-1（降级 P1 加固）/ P1-1~P1-10 / P2-1~P2-7 / P3 逐条并入对应章节，修订点均标注「审查修订」。
