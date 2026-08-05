# 实施记录——Node Sidecar S1：NodeJS↔Godot 通道落地

> **时间**：2026-08-05（Win 11 实机，dev 构建 `godot.windows.editor.dev.x86_64.exe`）
> **范围**：方案《Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》S1——
> NodeJS↔Godot WS/JSON-RPC 通道（sidecar_server + spawn/生命周期 + 双令牌）+ Node 侧 godot-client + WebBridge 能力面委托
> **衔接**：S0（commit `fd17e03447`，@baize/rpc + @baize/sidecar 骨架）；上位裁决《TS-CEF嵌入与NodeSidecar-设计.md》§4/§5.4
> **审查**：两轮 code review（3 reviewer × 2 轮），发现 23+16 项，修复闭环见 §6

---

## 1. 目标与验收

**问题**：编辑器进程内补一条 NodeJS sidecar 通道（Godot = WS server + spawn 管主，sidecar = 进程外服务宿主），
实现《设计》§4"三端两两直连"中的 NodeJS↔Godot 边，并为 Agent/LSP/资产等进程外服务提供宿主。

**验收**（方案 §7 S1，8 条）：
1. 启动编辑器 → 日志见 sidecar 进程拉起 → WS 握手成功（hello 应答）；
2. 外部 ws 客户端（带 token）连 Godot WS → `scene.create_node` 返回、场景树出现节点、`editor.undo` 可撤销；
3. ProcessSupervisor 跨平台：私有 env 注入、stdout/stderr 捕获、整树 kill、token 不出现在 argv/日志；
4. JSON-RPC 一致性：标准错误码 + data、string id、通知无响应、batch 拒绝、UTF-8、迟到/重复 id；
5. WS 资源限制：认证 deadline 3s、连接上限、~780KB 快照策略、慢客户端关闭；
6. 错误令牌拒绝；kill sidecar → 退避重启（第 4 次不重启）；dev 模式外部 watcher；BAIZE_NODE 缺失报错；
7. 4 个重叠 WebBridge 方法委托后回归；
8. `BAIZE_SIDECAR=0` 无 spawn；端口冲突清晰报错；退出竞态零残留；日志有界 + redaction。

## 2. 实现

### 2.1 Godot 侧（`modules/ai/`，新 4 文件 + 改 2 文件）

| 文件 | 职责 |
|---|---|
| `process_supervisor.{h,cpp}`（新） | **进程监督**（方案 §4.4 P1-1 前置）：per-spawn env 增量注入（不经 argv/全局环境）、cwd、stdout/stderr → `user://logs/sidecar.log`（>5MB 轮转 `.1`）、进程树 ownership——Windows = Job Object（`KILL_ON_JOB_CLOSE`，`CREATE_SUSPENDED` 挂起创建 + 绑定成功才恢复）+ 可继承 stdio 句柄；Unix = `fork` + `setsid` + `execve`（PATH 解析）+ `killpg` 杀组 + `waitpid` EINTR 重试 |
| `sidecar_server.{h,cpp}`（新） | **WS server**（复用引擎 websocket 模块，参照 EditorDebuggerServerWebSocket）：`TCPServer::listen(127.0.0.1, port 0)` + `WebSocketPeer::accept_stream` + 每帧 poll；**严格 JSON-RPC 2.0 自解析**（方案 §5.3 裁决，不依赖引擎内置 `JSONRPC` 类——其缺 error.data/string id/非 virtual）；双令牌握手（`sidecar.hello` 每次校验，认证 deadline 3s）；SemanticRegistry 透传（find + validate_args + handler）；资源预算（4 MiB 有界 message、连接上限 4、慢客户端 1009、UTF-8 字节输出预算）；崩溃退避重启（0.5/1/2/4/8s ×3，稳定 5min 重置）；独立进程监测（`is_running` 每帧）；退出编排（shutdown 通知直发 + 等 2s + kill 进程树） |
| `register_types.cpp`（改） | EDITOR 级 MessageQueue 第一帧启动 + uninitialize 退出编排（sidecar → ai_bridge → CEF，方案 §4.4 P1-6） |
| `config.py`（改） | `module_add_dependencies("ai", ["websocket"])`——**必需依赖**（不传 optional，否则 disable websocket 时链接失败，复审 P1） |

**协议**（方案 §5.1 线级合同）：一帧一 document；request id 一律 string；batch 显式拒绝（-32600）；server 拒 response 输入；错误码 -32601/-32602/-32000，内部字符串码入 `error.data.code`（如 `no_scene`/`unauthorized`）。

**env 契约**（方案 §4.3/§4.4）：`BAIZE_SIDECAR=0|1|dev`（默认 1）、`BAIZE_GODOT_WS_URL`（port 0 实际端口派生）、`BAIZE_GODOT_TOKEN`（spawn 生成）、`BAIZE_PROJECT_PATH`、`BAIZE_SIDECAR_ENTRY`（sidecar 入口，缺失明确报错）、`BAIZE_NODE`（>PATH）、dev 模式 `BAIZE_SIDECAR_TOKEN`（父环境显式提供，缺失拒绝）。

### 2.2 Node 侧（`web/runtime/`，2 文件 + 测试）

| 文件 | 职责 |
|---|---|
| `godot-client.ts`（新） | **Godot WS 客户端**：读 env → 连 Godot WS → `sidecar.hello`（token，deadline 3s）→ 复用 S0 `RpcClient`（send 注入/handleFrame 喂帧）；退避重连 0.5/1/2/4/8s ×10；epoch/generation 守卫（旧连接回调不污染新连接）；`sidecar.shutdown` 通知监听 → dispose（不重连）；invoke 仅实际 `readyState === OPEN` 放行（未就绪/CLOSING 确定性拒绝）；重试耗尽 `failAllPending` |
| `index.ts`（改） | env 分派：有 `BAIZE_GODOT_WS_URL` → GodotClient 主路径；无 → S0 测试 server（standalone/dev 调试，无 token 校验，注释标注） |

**测试**（`godot-client.test.ts`，7 用例）：握手成功/错误 token/断线重连（epoch 递增）/dispose/shutdown 监听/connecting 拒绝/重试耗尽（failAllPending spy ≥3 次）；mock Godot 协议形状与 C++ 一致（裸 result、-32000 + data.code）。

### 2.3 WebBridge 能力面合流（`modules/webview/`）

`scene.get_node_count` / `scene.create_node` / `editor.undo` / `editor.redo` 委托 `SemanticRegistry`
（find + validate_args + handler，与 AiBridge MCP 工具面共享同一份实现——方案 §5.2 P1-7）；
`create_node` 返回 `{instance_id, path, name}` 适配为裸 instance_id（前端 SDK 契约）；
`config.py` 声明 `module_add_dependencies("webview", ["ai"])`（必需依赖）。

## 3. 实机排坑（Windows，每个都有实测证据）

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | `CreateProcessW` 返回 87（ERROR_INVALID_PARAMETER） | 显式 lpEnvironment 未带 `CREATE_UNICODE_ENVIRONMENT` flag | creation_flags 加 `CREATE_UNICODE_ENVIRONMENT` |
| 2 | 同上 87 | `STARTF_USESTDHANDLES` 要求句柄可继承；`CreateFileW` 默认不可继承、`GetStdHandle` fallback 也不保证 | `SECURITY_ATTRIBUTES.bInheritHandle=TRUE` + NUL stdin，不 fallback |
| 3 | node 子进程启动即 abort（`ncrypto::CSPRNG` assert） | `CharStringT::size()` **含结尾 null**：env 块拼接用 size() 致条目间双 null，环境块首条即截断——子进程环境几乎为空 | 改用 `length()`（不含 null） |
| 4 | 退出后 sidecar 残留 | `taskkill /F` 强杀不走退出编排（预期）；正常退出路径验证 `--quit-after` 零残留 | 退出编排用 `--quit-after` 验证；stop() 直发 shutdown（不入队） |

## 4. 验证（实机，dev 构建）

### spawn 模式（验收 1/8）

```
[Sidecar] sidecar spawned: node D:/.../dist/index.js → ws://127.0.0.1:14821
[Sidecar] sidecar 握手成功（token 校验通过）
```
sidecar.log：`连接 Godot WS → Godot WS 已连接（epoch=1）→ sidecar.hello 应答: ok=true version=0.1.0`
`--quit-after 150` 正常退出 → `wmic` 确认 0 个 `dist/index.js` 进程（退出编排生效）。

### dev 模式 + 外部 ws 客户端（验收 2/4/6/7）

```
hello:      {"result":{"ok":true,"version":"0.1.0"}}
get_node_count: {"result":1}
create_node: {"result":{"instance_id":1563603050492,"name":"SidecarNode","path":"SidecarNode"}}
undo:       {"result":{}}
health:     {"result":{"ok":true,"services":[],"uptime_ms":8177}}
bad-hello（未认证首帧错误 token）: {"error":{"code":-32000,"data":{"code":"unauthorized"},"message":"token 校验失败"}}
```
- SemanticRegistry 透传 + 错误映射（-32000 + data.code）与 S0 协议向量一致（验收 4 交叉验证）；
- 错误 token 首帧现在能收到结构化错误（修复后），不再只是连接关闭；
- `BAIZE_SIDECAR=dev` 无 spawn、`uptime_ms` 从 session 基准计算。

## 5. 测试与构建

- web：sdk 27 + runtime **37** 测试全绿；typecheck 4 包；biome 16 文件无 error
- scons dev 构建：零错误（增量 ~20s）
- 构建脚本：`python misc/scripts/build.py --preset dev -j 16`

## 6. 审查闭环（3 reviewer × 2 轮）

**第一轮（23 项：P1×9、P2×14）**：
- C++（18）：Unix 编译（is_running const）/PATH 搜索/Job 绑定/进程监测/shutdown 只入队不 flush/认证错误被丢弃等 P1 6 项 + stdio 句柄泄漏/继承面/UTF-8 预算/日志无界/null peer 死循环/稳定重置/dev uptime/EINTR/env 大小写/chdir 等 P2 12 项
- Node（4）：shutdown 通知被 RpcClient 丢弃（P1）、未 OPEN invoke 静默丢帧（P1）、重试耗尽缺 failAllPending（P2）、mock 协议形状不符（P2）
- WebBridge（1）：webview 未声明 ai 模块依赖（P1）

**修复**：全部落地（详见 §2 各文件；认证错误改直发、shutdown 改直发、Job 改挂起创建、进程监测入 poll、invoke 按实际 socket 就绪、mock 对齐 C++ 线格式等）。

**复审（16 项）**：12 项确认修复；新发现 P1 1 项（**依赖 optional 不强制检查**——`module_add_dependencies` 第三参 True 只进 optional 槽位，`methods.py` 仅检查 required；两处 config.py 改必需依赖）+ P2 4 项已修（`_flush_out` 扣减口径、CLOSING 竞态 invoke、重试耗尽测试弱覆盖、其余边界）。

## 7. 遗留（P2，记录待后续，不阻断合入）

1. 已认证 dead peer drop（非文本帧/读帧失败/队列超限）不触发 kill+退避重启
2. 日志轮转 rename 失败忽略；轮转仅在 spawn 前（运行期无上限）
3. `ResumeThread`/`TerminateProcess`（Job 失败路径）返回值未检查
4. 重启 spawn 失败后 `next_spawn_ms_` 清零 → 剩余自动重试静默放弃
5. 已 reap PID 的 `kill_tree` 竞态（Unix，PID 复用风险）
6. 相对 PATH 项 + 子进程 cwd 解析不一致（Unix）
7. Windows 单 stdio 指定时另一流被 NUL 而非继承（与 Unix 语义不一致）
8. `stop()` 慢客户端（socket WOULDBLOCK）下 shutdown 泵送不完整

## 8. 相关文档

- 方案《Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》（S1 验收 §7、协议 §5、生命周期 §4.4）
- 《实施记录-AI-FIRST-P1-P2-语义接口与MCP.md》（能力面注册表/端口模式/排坑 §4）
- 两轮审查报告：`agent://ReviewCppGodot`（18 项）、`agent://ReviewNodeSide`（4 项）、`agent://ReviewWebBridge`（1 项）；复审 `agent://ReviewCppGodot2`、`agent://ReviewNodeSide2`、`agent://ReviewWebBridge2`
