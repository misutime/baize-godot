# att_nodejs_sidecar — NodeJS sidecar 通道

> 本 fork 特有模块（`att_` 前缀 = 别于上游 Godot）。通道协议与生命周期见
> 《doc/plans/Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》§4/§5。

## 职责（一句话）

**Godot 编辑器内的 NodeJS sidecar 通道**：WS server + spawn/重启/退出管主，
对接进程外的 Node sidecar（`web/runtime`，服务宿主）。

## 角色

| 侧 | 角色 |
|---|---|
| Godot（本模块） | WS server + 生命周期管主（spawn/退避重启/退出 kill 进程树） |
| Node sidecar（`web/runtime`，JS） | 服务宿主（Agent/LSP/资产），client 主动连回 |

**状态权威**：Godot（引擎）唯一权威；sidecar 只发命令、收事件，不缓存引擎状态。

## 生命周期

- **恒启用**（2026-08-05 决策）：sidecar 是编辑器地基（Agent/LSP/资产管线宿主），不提供关闭路径；
  旧 `BAIZE_SIDECAR=0` 弃用（警告后按默认 spawn）；无 Node 时报错并引导安装（一次性）。
- 启动：EDITOR 级 MessageQueue 第一帧 → `SidecarServer::start()` → listen(port 0) + spawn。
- 握手：`sidecar.hello`（token，认证 deadline 3s；每次调用校验）。
- 崩溃恢复：进程监测（`is_running` 每帧）+ 退避重启 0.5/1/2/4/8s ×3，稳定 5min 重置，
  第 4 次不再自动重启；spawn 失败同样进入退避重试。
- 退出：EDITOR uninitialize → shutdown 通知（直发 + poll 泵送）→ 等 2s → kill 进程树（Job Object / killpg）。

## env 契约

`BAIZE_SIDECAR=1|dev`（默认 1；dev = 外部自管，需显式 `BAIZE_SIDECAR_TOKEN`）、
`BAIZE_GODOT_WS_URL`（port 0 实际端口派生）、`BAIZE_GODOT_TOKEN`（spawn 生成）、
`BAIZE_PROJECT_PATH`、`BAIZE_SIDECAR_ENTRY`（sidecar 入口，缺失明确报错）、`BAIZE_NODE`（>PATH）。

## 协议（JSON-RPC 2.0 严格子集，方案 §5）

一帧一 document；request id 一律 string；batch 显式拒绝（-32600）；server 拒 response 输入；
错误码 -32601/-32602/-32000，内部字符串码入 `error.data.code`。
能力方法透传 `att_editor_ops` 的 `Registry`（不新增注册路径）。

## 文件结构

| 文件 | 角色 |
|---|---|
| `sidecar_server.{h,cpp}` | WS server + 双令牌认证 + JSON-RPC 分派 + 资源预算 + 生命周期管理 |
| `process_supervisor.{h,cpp}` | 进程监督：per-spawn env/cwd/stdio 重定向/进程树（Win Job Object / Unix killpg） |
| `register_types.{h,cpp}` | EDITOR 第一帧启动 + 退出编排 |

依赖：`att_editor_ops`（能力分派）+ `websocket`（引擎 wslay）。

## 相关文档

- 《Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》（§4 拓扑/生命周期、§5 协议）
- 《实施记录-NodeSidecar-S1-通道落地.md》（落地与排坑）
- `web/runtime/src/godot-client.ts`（Node 侧 client 实现与测试）
