/**
 * JSON-RPC 2.0 编解码（契约层共享：client 解析应答、server 解析请求）。
 * 自做严格解析（不依赖 JSON-RPC 库），语义与 Godot Provider 侧 C++ 实现一致。
 */
import type {
  RpcError,
  RpcFailureResponse,
  RpcNotification,
  RpcRequest,
  RpcResponse,
  RpcSuccessResponse,
} from "./types";

/** JSON-RPC 2.0 标准错误码 + 业务错误码。 */
export const RPC_ERROR = {
  PARSE_ERROR: -32700,
  INVALID_REQUEST: -32600,
  METHOD_NOT_FOUND: -32601,
  INVALID_PARAMS: -32602,
  INTERNAL_ERROR: -32603,
  /** 业务失败统一码：handler 返回 { ok:false, error } 时映射（内部字符串码入 error.data.code）。 */
  BIZ_ERROR: -32000,
} as const;

export function toRpcError(code: number, message: string, data?: RpcError["data"]): RpcError {
  return { code, message, ...(data === undefined ? {} : { data }) };
}

export function encodeSuccess(id: string, result: unknown): string {
  return JSON.stringify({ jsonrpc: "2.0", id, result } satisfies RpcSuccessResponse);
}

export function encodeError(id: string | null, error: RpcError): string {
  return JSON.stringify({ jsonrpc: "2.0", id, error } satisfies RpcFailureResponse);
}

export type DecodeResult =
  | { kind: "request"; request: RpcRequest }
  | { kind: "notification"; notification: RpcNotification }
  | { kind: "response"; response: RpcResponse }
  | { kind: "error"; error: RpcError; id: string | null };

/**
 * 解析一帧文本为 JSON-RPC 消息。严格性：
 * - 非法 JSON → parse error（id null）；
 * - 数组（batch）/非对象/非 2.0/响应歧义 → invalid request（合同显式拒绝 batch）；
 * - request id 必须为 string（合同）；response id 必须为 string|null 且恰含 result 或 error。
 */
export function decodeFrame(text: string): DecodeResult {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return { kind: "error", error: toRpcError(RPC_ERROR.PARSE_ERROR, "Parse error"), id: null };
  }
  if (Array.isArray(parsed)) {
    return {
      kind: "error",
      error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: batch 显式拒绝"),
      id: null,
    };
  }
  if (typeof parsed !== "object" || parsed === null) {
    return { kind: "error", error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request"), id: null };
  }
  const obj = parsed as Record<string, unknown>;
  if (obj.jsonrpc !== "2.0") {
    return {
      kind: "error",
      error: toRpcError(RPC_ERROR.INVALID_REQUEST, 'Invalid Request: jsonrpc 必须为 "2.0"'),
      id: null,
    };
  }
  if (typeof obj.method === "string") {
    if (Object.hasOwn(obj, "id")) {
      if (typeof obj.id !== "string") {
        return {
          kind: "error",
          error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: request id 必须为 string"),
          id: null,
        };
      }
      return {
        kind: "request",
        request: {
          jsonrpc: "2.0",
          id: obj.id,
          method: obj.method,
          ...(Object.hasOwn(obj, "params") ? { params: obj.params } : {}),
        },
      };
    }
    return {
      kind: "notification",
      notification: {
        jsonrpc: "2.0",
        method: obj.method,
        ...(Object.hasOwn(obj, "params") ? { params: obj.params } : {}),
      },
    };
  }
  // 无 method → 必须是 response：id string|null + 恰含 result 或 error
  if (!Object.hasOwn(obj, "id") || (obj.id !== null && typeof obj.id !== "string")) {
    return { kind: "error", error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request"), id: null };
  }
  const hasResult = Object.hasOwn(obj, "result");
  const hasError = Object.hasOwn(obj, "error");
  if (hasResult === hasError) {
    return {
      kind: "error",
      error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: response 必须恰含 result 或 error"),
      id: null,
    };
  }
  if (hasError) {
    const e = obj.error;
    if (
      typeof e !== "object" ||
      e === null ||
      typeof (e as RpcError).code !== "number" ||
      typeof (e as RpcError).message !== "string"
    ) {
      return {
        kind: "error",
        error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: error 对象非法"),
        id: null,
      };
    }
    return { kind: "response", response: { jsonrpc: "2.0", id: obj.id, error: e as RpcError } };
  }
  return { kind: "response", response: { jsonrpc: "2.0", id: obj.id, result: obj.result } };
}

/** 便捷：把任意 JSON-RPC 消息帧反序列化（测试/日志用）。 */
export function parseMessage(text: string): import("./types").RpcMessage | null {
  try {
    const parsed: unknown = JSON.parse(text);
    if (
      typeof parsed === "object" &&
      parsed !== null &&
      (parsed as Record<string, unknown>).jsonrpc === "2.0"
    ) {
      return parsed as import("./types").RpcMessage;
    }
    return null;
  } catch {
    return null;
  }
}
