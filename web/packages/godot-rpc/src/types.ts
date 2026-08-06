/**
 * JSON-RPC 2.0 消息类型（@baize/godot-rpc 契约层）。
 *
 * 线级合同（与 Godot Provider 侧 C++ 实现对齐）：
 * - 每个 WS text message = 恰好一个 JSON-RPC document；
 * - request id 一律 string（规避 C++ 数字→double 陷阱）；
 * - batch 请求显式拒绝（-32600）；
 * - 错误码：未知方法 -32601、参数校验 -32602、业务失败 -32000，内部字符串码放 error.data.code；
 * - 事件下行 = notification（Godot→消费方单向）。
 */

/** JSON-RPC 2.0 request（id 一律 string）。 */
export interface RpcRequest {
  jsonrpc: "2.0";
  id: string;
  method: string;
  params?: unknown;
}

/** JSON-RPC 2.0 notification（无 id；事件下行/单向通知）。 */
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

/**
 * 协议级方法名（Provider 握手/健康/订阅，非能力方法）。
 * 能力方法（scene.* 与 editor.* 等）由 Godot Provider 的 Catalog 声明，
 * 经 @baize/godot-sdk 的类型化绑定引用（不在此联合内）。
 */
export type ProtocolMethod = "hello" | "health" | "subscribe" | "unsubscribe";

// ---- 协议级方法 payload 类型 ----

export interface HelloParams {
  token: string;
}
export interface HelloResult {
  ok: boolean;
  version: string;
}
export type HealthParams = Record<string, never>;
export interface HealthResult {
  ok: boolean;
  uptimeMs: number;
  services: string[];
}
export type SubscribeParams = {
  events: string[];
};
export interface SubscribeResult {
  ok: boolean;
  events: string[];
}
