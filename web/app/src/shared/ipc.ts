/**
 * 主进程 ↔ preload ↔ 渲染进程共享的 IPC 契约（wire format）。
 * 同时被 tsconfig.node（electron/）与 tsconfig.web（src/renderer）引用——
 * 类型与通道名的唯一来源，避免内联声明漂移（P3-7）。
 */

/** Godot 进程/连接状态（下行 godot:process 事件载荷；视口面板数据源）。 */
export interface GodotProcessStatus {
  state: "starting" | "running" | "exited" | "error" | "restarting";
  code?: number | null;
  provider: "connecting" | "connected" | "disconnected";
}

/** preload 通过 contextBridge 暴露给渲染进程的能力面（window.godot）。 */
export interface GodotBridge {
  /** 调用 Godot Provider 能力方法（经主进程转发）。 */
  request: (method: string, params?: unknown) => Promise<unknown>;
  /** 订阅 Provider 下行事件。返回退订函数。 */
  onEvent: (listener: (method: string, params: unknown) => void) => () => void;
  /** 订阅 Godot 进程/连接状态（视口面板数据源）。返回退订函数。 */
  onProcessStatus: (listener: (status: GodotProcessStatus) => void) => () => void;
}

/** IPC 通道名（main/preload 唯一来源，避免字符串散落漂移）。 */
export const IPC = {
  request: "godot:request",
  event: "godot:event",
  process: "godot:process",
} as const;
