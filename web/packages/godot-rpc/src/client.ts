/**
 * JSON-RPC client：string id 生成 + req_id 配对 + 超时 + 通知下行。
 * 传输无关（send 回调 + handleFrame 喂帧）——ws/ipc/inproc 通道共用此配对层。
 */
import { decodeFrame } from "./codec";
import type { RpcError, RpcRequest } from "./types";

/** 业务调用错误（服务端 error 响应时 reject；含 code/data）。 */
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

/** 本地超时（悬空防护）。 */
export class RpcTimeoutError extends Error {
  constructor(method: string, timeoutMs: number) {
    super(`invoke 超时（${timeoutMs}ms）: ${method}`);
    this.name = "RpcTimeoutError";
  }
}

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (err: unknown) => void;
  timer: ReturnType<typeof setTimeout>;
}

export interface RpcClientOptions {
  /** 超时默认值（ms），默认 10000。 */
  defaultTimeoutMs?: number;
}

/** JSON-RPC client：id 生成 + 配对 + 超时 + 迟到应答丢弃 + 通知下行。 */
export class RpcClient {
  private seq = 0;
  private readonly pending = new Map<string, PendingCall>();
  private readonly notificationListeners = new Set<(method: string, params: unknown) => void>();
  private readonly defaultTimeoutMs: number;
  private disposed = false;

  constructor(
    private readonly send: (text: string) => void,
    options: RpcClientOptions = {},
  ) {
    this.defaultTimeoutMs = options.defaultTimeoutMs ?? 10000;
  }

  /** 发起调用：登记配对 → 发送帧。超时 reject RpcTimeoutError；业务/协议错误 reject RpcCallError。 */
  invoke<T = unknown>(method: string, params?: unknown, timeoutMs?: number): Promise<T> {
    if (this.disposed) {
      return Promise.reject(new Error("RpcClient 已 dispose"));
    }
    const effectiveTimeout = timeoutMs ?? this.defaultTimeoutMs;
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
        reject(new RpcTimeoutError(method, effectiveTimeout));
      }, effectiveTimeout);
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
          return; // 迟到/重复应答：丢弃
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
        // 当前拓扑：消费方 = client、Godot Provider = server——server 不发 request；收到即忽略
        return;
      case "error":
        // server 侧 parse/invalid 错误（无 id）：无配对目标，忽略
        return;
    }
  }

  /** 订阅服务端下行通知（事件下行）。返回退订函数。 */
  onNotification(listener: (method: string, params: unknown) => void): () => void {
    this.notificationListeners.add(listener);
    return () => {
      this.notificationListeners.delete(listener);
    };
  }

  /** 断线清理：以稳定错误拒绝全部 pending。 */
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
