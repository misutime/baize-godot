/**
 * Electron IPC transport：渲染进程 → 主进程转发 → Godot Provider。
 * 零依赖：不 import electron——调用方（preload/主进程）注入 request/onEvent 实现。
 *
 * 安全模型：token/端口不出渲染进程；req_id 配对在主进程侧完成，
 * 渲染进程只经注入的受控通道发请求/收事件。
 */
import type { Transport } from "./transport";

export interface IpcTransportDeps {
  /** 注入的 IPC 请求通道（preload contextBridge 暴露；主进程侧转发到 Godot WS 并完成配对）。 */
  request: (method: string, params: unknown) => Promise<unknown>;
  /** 注入的事件下行通道（preload 转发主进程推送的事件 notification）；返回退订函数。 */
  onEvent: (listener: (method: string, params: unknown) => void) => () => void;
  log?: (message: string) => void;
}

/** 创建基于 Electron IPC 的 Transport（配对在主进程，此处为纯转发）。 */
export function createIpcTransport(deps: IpcTransportDeps): Transport {
  const log = deps.log ?? (() => {});
  let eventUnsub: (() => void) | null = null;

  return {
    async request<T = unknown>(method: string, params?: unknown): Promise<T> {
      const result = await deps.request(method, params);
      return result as T;
    },
    onEvent(listener) {
      if (eventUnsub) {
        log("[ipc] onEvent 重复订阅：先退订旧监听");
        eventUnsub();
      }
      eventUnsub = deps.onEvent(listener);
      return () => {
        eventUnsub?.();
        eventUnsub = null;
      };
    },
    close() {
      eventUnsub?.();
      eventUnsub = null;
    },
  };
}
