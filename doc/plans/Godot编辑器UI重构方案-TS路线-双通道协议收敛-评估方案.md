# Godot编辑器UI重构方案-TS路线-双通道协议收敛-评估方案

> **状态**：2026-08-05 编写，评估用（非实施）。衔接《WebUI架构-桥协议与前端SDK.md》（§1.1/§3）与
> 《NodeSidecar落地-方案.md》（§5）。背景：`web/sdk` 移入 `web/packages/` 后，web/ 侧三个 TS 包
> （sdk/runtime/rpc）的通道边界已清晰，暴露两通道传输/协议层的机制同构，本文评估是否及如何收敛。
>
> **结论先行**：机制同构真实存在，但两通道（传输、信封、角色、错误语义）四维不同，直接统一线协议
> 成本主要在 C++ 侧（web_bridge 改信封形态），收益是 sdk 复用 @baize/rpc 类型与配对逻辑、三端类型
> 单一来源。建议：**短期不动，触发条件出现（S2 sdk 接入 sidecar JSON-RPC、或三端类型漂移 CI）时按
> 方案 A 局部收敛**；方案 B（共享配对机制库）不推荐单独立项。

---

## 1. 背景与动机

web/ 侧现有两个对外通道（能力面同为 `modules/att_editor_ops` Registry，见 AGENTS.md 分层规则）：

| 通道 | TS 客户端 | C++ 服务端 | 传输 | 线协议 |
|---|---|---|---|---|
| CEF WebDock | `web/packages/sdk`（@baize/ui-sdk） | `modules/att_webview`（web_bridge.cpp + CefViewCore） | CefViewClient 桥对象（CEF IPC） | 自定义信封 |
| Node sidecar | `web/runtime`（@baize/sidecar） | `modules/att_nodejs_sidecar`（sidecar_server.cpp） | WebSocket | JSON-RPC 2.0 严格子集 |

动机（评估是否值得收敛）：

1. **机制同构**：sdk `transport.ts` 的 req_id 配对 + 超时 + 迟到丢弃，与 runtime `jsonrpc.ts` 的
   `RpcClient`（pending + 超时 + `failAllPending`）是同一种机制写了两遍；`registry.ts` 的
   defineMethod/defineEvent 与 @baize/rpc 的 ProtocolMethod/payload 类型也有重复声明风险。
2. **@baize/rpc 的定位声明**（`packages/rpc/src/index.ts` 头注释）已把 CEF sdk 列为 S2 消费者——
   届时 sdk 需要第二套传输（JSON-RPC WS 连 sidecar），类型来源必须收敛，否则两份协议类型漂移。
3. **三端类型对齐**：Godot sidecar_server（C++ 手写对齐）↔ Node sidecar ↔ CEF sdk 的消息类型
   目前靠人肉对齐，类型漂移 CI 被列为后续工作（NodeSidecar 方案 §5.3）。

## 2. 两通道现状对照（2026-08-05 实测源码）

| 维度 | CEF 通道（sdk ↔ web_bridge.cpp） | sidecar 通道（runtime ↔ sidecar_server.cpp） |
|---|---|---|
| 传输介质 | `window.CefViewClient.invoke(method, args)` + `addEventListener`（CefViewCore IPC） | WS（`ws` 库；sidecar 为 client 主动连回 Godot WS server） |
| 请求信封 | `invoke(method, JSON.stringify({req_id, ...params}))`；req_id 为 SDK 生成字符串（规避 C++ double 陷阱） | JSON-RPC 2.0：`{jsonrpc:"2.0", id, method, params}`；id 一律 string |
| 应答信封 | 事件下行 `method_result`：`{req_id, ok:true, result}` / `{req_id, ok:false, error:{code,message}}`（web_bridge.cpp `_respond`） | `{jsonrpc:"2.0", id, result}` / `{jsonrpc:"2.0", id, error:{code,message,data}}` |
| 错误语义 | 字符串 code 直传（`invalid_params`/`method_not_found`/Registry error.code） | 数字码 -32601/-32602/-32000 + 内部字符串码 `data.code`（§5.1 映射） |
| 事件/通知下行 | `TriggerEvent` 原生事件（editor.* 事件源，帧轮询 diff 推送） | notification（白名单：sidecar.shutdown 等；事件方向限定 Godot→sidecar） |
| req_id 配对 | `pending: Map<string, PendingCall>` + 超时 10s + 未知 req_id 丢弃 | `RpcClient` pending + 超时 + 断线/上限 `failAllPending` 确定性拒绝 |
| 类型化声明 | `registry.ts` defineMethod/defineEvent → bridge.ts 实例（scene.*/editor.*） | @baize/rpc 类型（RpcRequest/RpcResponse/ProtocolMethod/sidecar.* payload） |
| 能力分派 | `Registry::find` + `validate_args` + handler（web_bridge 委托） | `Registry::find` 透传（sidecar_server `_dispatch`） |
| 错误兜底 | 桥注入缺失显式抛错；超时 reject `{code:"timeout"}` | 未就绪态确定性拒绝；本地超时 `RpcTimeoutError` |

**同构点（收敛候选）**：① req_id/id 生成与配对生命周期；② 超时悬空防护；③ 迟到应答丢弃；
④ 类型化方法/事件声明模式；⑤ 能力来源（Registry）与参数校验语义。

**异构点（不能共享的）**：传输介质、信封字段、错误码体系、client/server 角色、事件方向白名单。

## 3. 收敛方案选项

### 方案 A：统一线协议为 JSON-RPC 2.0（全量收敛）

CEF 通道也改标准信封，sdk 复用 @baize/rpc 类型 + 共享编解码/配对。

- **TS 侧改动**：
  - sdk `transport.ts` 改发 `{jsonrpc:"2.0", id, method, params}`，应答改解析 `result/error`；
    `registry.ts`/`bridge.ts` 方法名（scene.*/editor.*）不变，仅信封形态变；可复用
    @baize/rpc 的 RpcRequest/RpcResponse 类型。
  - @baize/rpc 从纯类型包变为"类型 + 运行时"（或拆出 `@baize/rpc/client` 子路径）：编解码 +
    RpcClient 配对逻辑 + 超时。**体积约束**：sdk 现 gzip 1.13kB（file:// 面板、体积敏感），
    JSON-RPC 编解码 + 配对估计 ≤2kB gzip，需实测确认。
  - runtime `jsonrpc.ts` 的 `RpcClient` 迁入共享包，`GodotClient` 保留（传输专属）。
- **C++ 侧改动（主要成本）**：
  - `web_bridge.cpp` `handle_invoke`/`_respond` 改标准信封（解析 `params` 里的 id、应答带
    `jsonrpc`/`id`/`result`/`error`），错误映射为数字码 + `data.code`（与 sidecar_server 对齐）。
  - `sidecar_server.cpp` 无需动（已是标准）。
  - 事件下行：editor.* 事件仍走 `TriggerEvent`（通道专属，不归 JSON-RPC 信封管）——事件层不受影响。
- **收益**：三端消息类型单一来源（@baize/rpc）；sdk/runtime 共享配对实现；S2 时 sdk 接 sidecar
  通道自然复用；类型漂移 CI 有单点可查。
- **成本/风险**：C++ web_bridge 协议形态变更 + sdk transport 重构 + 1a 实测过的调用约定回归
  风险；CEF 通道已稳定运行（属性面板全链路通），改动需完整重验 MVP 四条验收。
- **时机**：S2（sdk 接 sidecar JSON-RPC）启动时一并做，单独做不划算。

### 方案 B：仅共享 TS 配对机制（传输无关的 correlation 层）

抽一个零依赖 TS 包（或 @baize/rpc 加运行时子路径），封装 pending/配对/超时/迟到丢弃，两通道
各自保留传输适配（sdk 喂 CefViewClient、runtime 喂 ws），信封保持现状。

- **改动**：sdk `transport.ts` 与 runtime `RpcClient` 的 pending 核心抽公共；`bridge.ts`/`registry.ts`
  不动；C++ 侧零改动。
- **收益**：消除机制重复，风险最小（C++ 不动）。
- **成本/风险**：信封仍是两套，@baize/rpc 的 ProtocolMethod 与 sdk bridge 的方法名声明仍各自维护
  （类型漂移问题未解）；抽层本身引入抽象，sdk 体积微增。**收效有限——机制重复不是主要矛盾，
  类型单一来源才是。**

### 方案 C：维持现状（明确不收敛）

- **理由**：两通道服务不同宿主（CEF 面板 vs Node sidecar）、不同生命周期（进程内桥 vs 进程外
  WS + 重连/认证），信封差异是刻意为之（WebUI 架构文档 §1.1 决策：CefViewClient 自建配对约
  50 行，换来单通道统一）；sdk 零依赖体积是硬约束。
- **代价**：重复的配对机制 + 两份协议类型声明，漂移靠人肉对齐（NodeSidecar 方案已把类型漂移
  CI 列为后续）。

## 4. 关键决策点（需用户裁决）

1. **是否接受 sdk 引入运行时依赖**（@baize/rpc 运行时子路径，~2kB gzip）换取类型单点？
   若坚持 sdk 零依赖，方案 A 只能共享"类型"、不能共享"配对实现"（配对仍各写一遍，收敛不彻底）。
2. **CEF 通道信封改动时机**：跟随 S2 一起做（推荐），还是单独排期？
3. **C++ 侧错误码统一**：web_bridge 改为数字码 + data.code（与 sidecar 一致）还是保留字符串码
   （若保留，方案 A 的"错误语义对齐"目标打折扣）？

## 5. 建议路线与触发条件

- **当前（2026-08-05）**：维持现状（方案 C）。文档留存，不产生代码改动。
- **触发条件（满足任一即启动评估→实施）**：
  1. S2 排期：sdk 接入 sidecar JSON-RPC 通道（@baize/rpc 头注释已声明的消费方）——届时按方案 A，
     类型与配对一并收敛，sdk 获得第二传输；
  2. 三端类型漂移 CI 落地（NodeSidecar 方案 §5.3）——若 CI 暴露 sdk bridge 与 rpc 类型漂移，
     优先方案 A 的类型收敛部分；
  3. 新增第三个通道（如 Node MCP，att_editor_ops README 已预告）——协议形态在通道三设计时定，
     倒逼统一决策。
- **方案 B 不单独立项**：机制重复可通过方案 A 顺带解决，单独抽层收益不足以覆盖抽象成本。

## 6. 验收参考（若实施方案 A）

- sdk 单测（现 27 例）信封用例改写为 JSON-RPC 2.0 形态；runtime 37 例不变（信封未变）；
- typecheck 4 包 + sdk 构建体积实测（gzip 增量 ≤2kB 目标）；
- C++ 侧 web_bridge 信封改造后，实机重验 MVP 四条验收（WebUI 实现文档 §9）；
- 三端类型对齐：@baize/rpc 为唯一类型来源，C++ sidecar_server 类型漂移 CI 接入。

---

## 附：引用索引

- 通道协议：`doc/plans/Godot编辑器UI重构方案-TS路线-WebUI架构-桥协议与前端SDK.md`（§1.1/§3.2）
- sidecar 协议：`doc/plans/Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md`（§4/§5）
- 能力面分层：`AGENTS.md` §13 + `modules/att_editor_ops/README.md`（Registry 唯一事实源）
- 实现现状：`web/packages/sdk/src/{transport,registry,bridge}.ts`、`web/runtime/src/{jsonrpc,godot-client}.ts`、
  `web/packages/rpc/src/index.ts`、`modules/att_webview/web_bridge.cpp`、`modules/att_nodejs_sidecar/sidecar_server.cpp`
