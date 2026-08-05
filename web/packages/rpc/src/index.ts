/**
 * 三端共享 JSON-RPC 2.0 消息类型（纯类型包，零运行时、无生成 JS）。
 *
 * 线级合同见《doc/plans/Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》§5.1（审查修订 P1-3）：
 * - 每个 WS text message = 恰好一个 JSON-RPC document；
 * - request id 一律 string（SDK 生成，规避 C++ 数字→double 陷阱）；
 * - batch 请求显式拒绝（-32600）；
 * - 错误码：未知方法 -32601、参数校验 -32602、业务失败 -32000，内部字符串码放 error.data.code。
 *
 * 消费方：Node sidecar（web/runtime）、CEF sdk（web/sdk，S2 接入）、C++ sidecar_server（手写对齐，S1）。
 */

/** JSON-RPC 2.0 request（id 一律 string）。 */
export interface RpcRequest {
  jsonrpc: "2.0";
  id: string;
  method: string;
  params?: unknown;
}

/** JSON-RPC 2.0 notification（无 id；仅白名单方向：Godot→sidecar 事件下行、sidecar→CEF 服务进度）。 */
export interface RpcNotification {
  jsonrpc: "2.0";
  method: string;
  params?: unknown;
}

/** JSON-RPC 2.0 error 对象；内部字符串码放 data.code。 */
export interface RpcError {
  code: number;
  message: string;
  data?: { code?: string; [key: string]: unknown };
}

export interface RpcSuccessResponse {
  jsonrpc: "2.0";
  id: string | null;
  result: unknown;
}

export interface RpcFailureResponse {
  jsonrpc: "2.0";
  id: string | null;
  error: RpcError;
}

export type RpcResponse = RpcSuccessResponse | RpcFailureResponse;
export type RpcMessage = RpcRequest | RpcNotification | RpcResponse;

/** 协议方法名（§5.2；S1 起透传 SemanticRegistry 方法，类型随之扩展）。 */
export type ProtocolMethod =
  | "sidecar.hello"
  | "sidecar.health"
  | "sidecar.echo"
  | "sidecar.subscribe"
  | "sidecar.unsubscribe";

// ---- 协议方法 payload 类型（S0 骨架；方法/事件 payload 随 S1/S2 填充） ----

export interface SidecarHelloParams {
  token: string;
}
export interface SidecarHelloResult {
  ok: boolean;
  version: string;
}
export type SidecarHealthParams = Record<string, never>;
export interface SidecarHealthResult {
  ok: boolean;
  uptimeMs: number;
  services: string[];
}
export interface SidecarEchoParams {
  text: string;
}
export interface SidecarEchoResult {
  text: string;
  ts: number;
}
