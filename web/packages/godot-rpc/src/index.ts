/**
 * @baize/godot-rpc：JSON-RPC 契约与传输核心（零运行时依赖，纯 TS）。
 *
 * 契约层（类型 + 编解码）：types / codec
 * 客户端核心（配对/超时/通知）：client（RpcClient）
 * 传输抽象与实现：transport（Transport 接口）/ ws（原生 WebSocket）/ ipc（Electron IPC 注入）
 */
export * from "./types";
export * from "./codec";
export * from "./client";
export * from "./transport";
export { createWsTransport } from "./ws";
export type { WsTransportOptions } from "./ws";
export { createIpcTransport } from "./ipc";
export type { IpcTransportDeps } from "./ipc";
