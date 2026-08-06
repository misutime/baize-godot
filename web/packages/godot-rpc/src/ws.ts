/**
 * WS transport：WebSocket 通道 + RpcClient 配对 → Transport。
 * 使用标准 WebSocket API（lib.dom 类型）——浏览器与 Node 22+ 原生 WebSocket 均可用，零运行时依赖。
 *
 * 重连策略：断开后按 backoffSeconds 递增退避重连，超过 maxReconnects 或 close() 后停止。
 */
import { RpcClient } from "./client";
import type { Transport, TransportOptions, TransportState } from "./transport";

const DEFAULT_BACKOFF_SECONDS = [0.5, 1, 2, 4, 8];
const DEFAULT_MAX_RECONNECTS = 5;

export interface WsTransportOptions extends TransportOptions {
  url: string;
  /** 退避序列（秒），默认 0.5/1/2/4/8。 */
  backoffSeconds?: number[];
  /** 最大重连次数（0 = 不重连），默认 5。 */
  maxReconnects?: number;
}

export function createWsTransport(options: WsTransportOptions): Transport {
  const backoffSeconds = options.backoffSeconds ?? DEFAULT_BACKOFF_SECONDS;
  const maxReconnects = options.maxReconnects ?? DEFAULT_MAX_RECONNECTS;
  const log = options.log ?? (() => {});

  let ws: WebSocket | null = null;
  let rpc: RpcClient | null = null;
  let state: TransportState = "idle";
  let closed = false;
  let reconnectCount = 0;
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  let generation = 0; // 防旧连接回调穿越（关闭旧连接后忽略其事件）
  // 事件监听器：连接建立/重连后绑定到当前 RpcClient（未连接时订阅不丢失）
  const eventListeners = new Set<(method: string, params: unknown) => void>();

  function bindEventListeners(): void {
    for (const listener of eventListeners) {
      rpc?.onNotification(listener);
    }
  }

  function setState(next: TransportState): void {
    if (state !== next) {
      state = next;
      options.onStateChange?.(next);
    }
  }

  function failPending(reason: string): void {
    if (rpc) {
      rpc.failAllPending(reason);
    }
  }

  function scheduleReconnect(): void {
    if (closed || reconnectCount >= maxReconnects) {
      setState("failed");
      return;
    }
    const delayMs = Math.min(backoffSeconds[reconnectCount] ?? backoffSeconds[backoffSeconds.length - 1], 60) * 1000;
    reconnectCount++;
    setState("reconnecting");
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, delayMs);
  }

  function connect(): void {
    if (closed) {
      return;
    }
    const gen = ++generation;
    setState("connecting");
    let socket: WebSocket;
    try {
      socket = new WebSocket(options.url);
    } catch (err) {
      log(`[ws] WebSocket 创建失败: ${err instanceof Error ? err.message : String(err)}`);
      failPending("连接创建失败");
      scheduleReconnect();
      return;
    }
    ws = socket;

    socket.onopen = () => {
      if (gen !== generation || closed) {
        return;
      }
      // 注意：重连计数不在 onopen 重置——握手失败（连接建立但认证被拒）也是失败，
      // 计数由 resetReconnectBudget（上层握手成功后）显式归零。
      rpc = new RpcClient((text) => socket.send(text));
      bindEventListeners(); // 重连后事件监听器绑定到新 RpcClient
      setState("connected");
    };

    socket.onmessage = (ev) => {
      if (gen !== generation || closed) {
        return;
      }
      if (typeof ev.data === "string") {
        rpc?.handleFrame(ev.data);
      }
    };

    socket.onclose = () => {
      if (gen !== generation || closed) {
        return;
      }
      failPending("连接关闭");
      rpc = null;
      scheduleReconnect();
    };

    socket.onerror = () => {
      // onclose 随后触发并处理重连；此处仅记录
      log("[ws] 连接错误");
    };
  }

  // 创建即开始连接（首个连接立即发起；close 后不再重连）
  connect();

  return {
    request<T = unknown>(method: string, params?: unknown, timeoutMs?: number): Promise<T> {
      const client = rpc;
      if (!client) {
        return Promise.reject(new Error(`传输未就绪（${state}）：${method}`));
      }
      return client.invoke<T>(method, params, timeoutMs);
    },
    onEvent(listener) {
      eventListeners.add(listener);
      rpc?.onNotification(listener); // 已连接：立即绑定
      return () => {
        eventListeners.delete(listener);
      };
    },
    resetReconnectBudget(): void {
      reconnectCount = 0;
    },
    close(): void {
      closed = true;
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      if (rpc) {
        rpc.dispose();
        rpc = null;
      }
      if (ws) {
        const socket = ws;
        ws = null;
        socket.onclose = null;
        socket.close();
      }
      setState("closed");
    },
  };

  // 创建即开始连接（首个连接立即发起；close 后不再重连）
  connect();
}
