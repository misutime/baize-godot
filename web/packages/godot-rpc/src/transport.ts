/**
 * Transport 接口：能力面客户端（@baize/godot-sdk）与底层通道之间的统一抽象。
 * 实现：ws（Node/主进程直连，原生 WebSocket）、ipc（Electron 渲染进程经主进程转发）、
 * inproc（未来 QuickJS 进程内直调）。
 *
 * 语义：
 * - request：发起调用并等待应答（配对/超时/错误映射由实现处理）；失败 reject RpcCallError/RpcTimeoutError/Error；
 * - onEvent：订阅服务端下行事件（notification），返回退订函数；
 * - close：关闭连接（幂等）。
 */
export interface Transport {
  request<T = unknown>(method: string, params?: unknown, timeoutMs?: number): Promise<T>;
  onEvent(listener: (method: string, params: unknown) => void): () => void;
  close(): void;
  /** 重置重连预算（上层认证/会话成功后调用；no-op 实现可省略语义）。 */
  resetReconnectBudget?(): void;
  /** 断开当前连接并触发重连（认证失败等场景；不置终态，区别于 close）。 */
  disconnectReconnect?(): void;
}

/** 连接状态（ws 传输上报，供上层显示/重连编排）。 */
export type TransportState = "idle" | "connecting" | "connected" | "reconnecting" | "failed" | "closed";

export interface TransportOptions {
  onStateChange?: (state: TransportState) => void;
  log?: (message: string) => void;
}
