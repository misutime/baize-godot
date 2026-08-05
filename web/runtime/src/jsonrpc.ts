/**
 * JSON-RPC 2.0 薄层：编解码 + server 分派 + client req_id 配对。
 * 线级合同见《doc/plans/Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》§5.1（审查修订 P1-2/P1-3）：
 * - 每个 WS text message = 恰好一个 JSON-RPC document；
 * - request id 一律 string；batch 显式拒绝（-32600）；
 * - 错误码：未知方法 -32601、参数校验 -32602、业务失败 -32000（内部字符串码入 error.data.code）；
 * - 业务语义 { ok,result } / { ok:false,error } 与 SemanticRegistry 一致，映射在分派层一次完成（§5.1）。
 *
 * 说明：C++ 侧 sidecar_server 按本层语义自做严格解析（§5.3 裁决，不依赖引擎内置 JSONRPC 类）；
 * 本文件是 Node 侧参照实现 + 协议向量 fixture 的来源。
 */
import type {
  RpcError,
  RpcFailureResponse,
  RpcMessage,
  RpcNotification,
  RpcRequest,
  RpcResponse,
  RpcSuccessResponse,
} from "@baize/rpc";

/** JSON-RPC 2.0 标准错误码 + 业务错误码（§5.1）。 */
export const RPC_ERROR = {
  PARSE_ERROR: -32700,
  INVALID_REQUEST: -32600,
  METHOD_NOT_FOUND: -32601,
  INVALID_PARAMS: -32602,
  INTERNAL_ERROR: -32603,
  /** 业务失败统一码：SemanticRegistry handler 返回 { ok:false, error } 时映射（内部字符串码入 error.data.code）。 */
  BIZ_ERROR: -32000,
} as const;

/** handler 返回值语义 = SemanticRegistry 的 { ok, result } / { ok:false, error }（§5.1，映射在分派层）。 */
export type HandlerResult =
  | { ok: true; result: unknown }
  | { ok: false; error: { code: string; message: string } };

export type Handler = (params: unknown) => HandlerResult | Promise<HandlerResult>;

function toRpcError(code: number, message: string, data?: RpcError["data"]): RpcError {
  return { code, message, ...(data === undefined ? {} : { data }) };
}

function encodeSuccess(id: string, result: unknown): string {
  return JSON.stringify({ jsonrpc: "2.0", id, result } satisfies RpcSuccessResponse);
}

function encodeError(id: string | null, error: RpcError): string {
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
 * - request id 必须为 string（合同 §5.1）；response id 必须为 string|null 且恰含 result 或 error。
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
      error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: batch 显式拒绝（§5.1 线级合同）"),
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
          error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: request id 必须为 string（§5.1）"),
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

/** JSON-RPC server 分派：注册 handler + 按帧处理（§5.2 能力面分发语义的 Node 侧参照）。 */
export class JsonRpcDispatcher {
  private readonly handlers = new Map<string, Handler>();

  register(method: string, handler: Handler): void {
    this.handlers.set(method, handler);
  }

  unregister(method: string): void {
    this.handlers.delete(method);
  }

  /** 处理一帧文本，返回待发送的响应文本；通知无响应返回 null。 */
  async handleFrame(text: string): Promise<string | null> {
    const decoded = decodeFrame(text);
    switch (decoded.kind) {
      case "error":
        return encodeError(decoded.id, decoded.error);
      case "notification": {
        // 通知方向白名单（§5.1）在 S1/S2 建立；此处按标准语义执行 handler 且无响应。
        const handler = this.handlers.get(decoded.notification.method);
        if (handler) {
          try {
            await handler(decoded.notification.params);
          } catch {
            // 通知无 id 通道，失败不产生响应；记录由调用方负责
          }
        }
        return null;
      }
      case "request": {
        const { id, method, params } = decoded.request;
        const handler = this.handlers.get(method);
        if (!handler) {
          return encodeError(id, toRpcError(RPC_ERROR.METHOD_NOT_FOUND, `Method not found: ${method}`));
        }
        try {
          const result = await handler(params);
          if (typeof result === "object" && result !== null && "ok" in result) {
            if (result.ok) {
              return encodeSuccess(id, result.result);
            }
            // { ok:false, error:{code,message} } → -32000 + data.code（§5.1 映射，内部码不入数值 error.code）
            const err = result.error;
            return encodeError(id, toRpcError(RPC_ERROR.BIZ_ERROR, err.message, { code: err.code }));
          }
          return encodeError(
            id,
            toRpcError(RPC_ERROR.INTERNAL_ERROR, "handler 返回值非法（应为 {ok,result}/{ok:false,error}）"),
          );
        } catch (e) {
          return encodeError(
            id,
            toRpcError(RPC_ERROR.INTERNAL_ERROR, e instanceof Error ? e.message : String(e)),
          );
        }
      }
      case "response":
        // 合同：Godot 侧不发 request，server 拒绝 response 输入（§5.1）
        return encodeError(
          null,
          toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: 服务端不接受 response 输入"),
        );
    }
  }
}

/** 业务调用错误（服务端 error 响应拒绝时 reject；含 code/data）。 */
export class RpcCallError extends Error {
  readonly code: number;
  readonly data?: RpcError["data"];

  constructor(code: number, message: string, data?: RpcError["data"]) {
    super(message);
    this.name = "RpcCallError";
    this.code = code;
    this.data = data;
  }
}

/** 本地超时（悬空防护，与 sdk transport 同构）。 */
export class RpcTimeoutError extends Error {
  constructor(method: string, timeoutMs: number) {
    super(`invoke 超时（${timeoutMs}ms）: ${method}`);
    this.name = "RpcTimeoutError";
  }
}

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (err: unknown) => void;
  timer: NodeJS.Timeout;
}

/** JSON-RPC client：string id 生成 + req_id 配对 + 超时 + 通知下行（S1 godot-client 复用）。 */
export class RpcClient {
  private seq = 0;
  private readonly pending = new Map<string, PendingCall>();
  private readonly notificationListeners = new Set<(method: string, params: unknown) => void>();
  private disposed = false;

  constructor(private readonly send: (text: string) => void) {}

  /** 发起调用：登记配对 → 发送帧。超时 reject RpcTimeoutError；业务/协议错误 reject RpcCallError。 */
  invoke<T = unknown>(method: string, params?: unknown, timeoutMs = 10000): Promise<T> {
    if (this.disposed) {
      return Promise.reject(new Error("RpcClient 已 dispose"));
    }
    const id = `rpc_${++this.seq}`;
    const request: RpcRequest = {
      jsonrpc: "2.0",
      id,
      method,
      ...(params === undefined ? {} : { params }),
    };
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new RpcTimeoutError(method, timeoutMs));
      }, timeoutMs);
      this.pending.set(id, {
        resolve: resolve as (value: unknown) => void,
        reject,
        timer,
      });
      this.send(JSON.stringify(request));
    });
  }

  /** 处理服务端下行帧：响应按 id 配对；通知转发监听器；未知 id 丢弃（迟到应答）。 */
  handleFrame(text: string): void {
    if (this.disposed) {
      return;
    }
    const decoded = decodeFrame(text);
    switch (decoded.kind) {
      case "response": {
        const id = decoded.response.id;
        if (typeof id !== "string") {
          return; // parse error 等无 id 响应：无配对目标，丢弃
        }
        const pending = this.pending.get(id);
        if (!pending) {
          return; // 迟到/重复应答：丢弃（测试断言 pendingCount 不变）
        }
        clearTimeout(pending.timer);
        this.pending.delete(id);
        if ("error" in decoded.response) {
          pending.reject(
            new RpcCallError(
              decoded.response.error.code,
              decoded.response.error.message,
              decoded.response.error.data,
            ),
          );
        } else {
          pending.resolve(decoded.response.result);
        }
        return;
      }
      case "notification": {
        for (const listener of this.notificationListeners) {
          listener(decoded.notification.method, decoded.notification.params);
        }
        return;
      }
      case "request":
        // 当前拓扑 sidecar = client、Godot = server——server 不发 request；收到即忽略（S1 复核）
        return;
      case "error":
        // 服务端 parse/invalid 错误（无 id）：无配对目标，忽略
        return;
    }
  }

  /** 订阅服务端下行通知（事件面 S1/S2 使用）。返回退订函数。 */
  onNotification(listener: (method: string, params: unknown) => void): () => void {
    this.notificationListeners.add(listener);
    return () => {
      this.notificationListeners.delete(listener);
    };
  }

  /** 断线清理：以稳定错误拒绝全部 pending（S1 崩溃恢复/退出时调用）。 */
  failAllPending(reason: string): void {
    for (const [id, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error(reason));
      this.pending.delete(id);
    }
  }

  /** 当前挂起调用数（测试断言）。 */
  pendingCount(): number {
    return this.pending.size;
  }

  dispose(): void {
    this.disposed = true;
    this.failAllPending("RpcClient 已 dispose");
    this.notificationListeners.clear();
  }
}

/** 便捷：把任意 JSON-RPC 消息帧反序列化（测试/日志用）。 */
export function parseMessage(text: string): RpcMessage | null {
  try {
    const parsed: unknown = JSON.parse(text);
    if (
      typeof parsed === "object" &&
      parsed !== null &&
      (parsed as Record<string, unknown>).jsonrpc === "2.0"
    ) {
      return parsed as RpcMessage;
    }
    return null;
  } catch {
    return null;
  }
}
