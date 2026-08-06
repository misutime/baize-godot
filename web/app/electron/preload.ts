/**
 * preload：contextBridge 暴露受控 API 给渲染进程。
 * 安全模型：渲染进程只拿到 { request, onEvent }——不暴露 ipcRenderer 本身、
 * 不暴露 token/端口；能力方法名与参数由主进程校验后转发。
 * 编译：tsc（TS7）→ dist-electron/preload.js（CommonJS，sandbox 兼容）。
 */
import { contextBridge, ipcRenderer, type IpcRendererEvent } from "electron";

export type GodotProcessStatus = {
  state: "starting" | "running" | "exited" | "error" | "restarting";
  code?: number | null;
  provider: "connecting" | "connected" | "disconnected";
};

export interface GodotBridge {
  /** 调用 Godot Provider 能力方法（经主进程转发）。 */
  request: (method: string, params?: unknown) => Promise<unknown>;
  /** 订阅 Provider 下行事件。返回退订函数。 */
  onEvent: (listener: (method: string, params: unknown) => void) => () => void;
  /** 订阅 Godot 进程/连接状态（视口面板数据源）。返回退订函数。 */
  onProcessStatus: (listener: (status: GodotProcessStatus) => void) => () => void;
}

const bridge: GodotBridge = {
  request: (method, params) => ipcRenderer.invoke("godot:request", method, params),
  onEvent: (listener) => {
    const handler = (_e: IpcRendererEvent, ev: { method: string; params: unknown }): void => {
      listener(ev.method, ev.params);
    };
    ipcRenderer.on("godot:event", handler);
    return () => {
      ipcRenderer.removeListener("godot:event", handler);
    };
  },
  onProcessStatus: (listener) => {
    const handler = (_e: IpcRendererEvent, status: GodotProcessStatus): void => {
      listener(status);
    };
    ipcRenderer.on("godot:process", handler);
    return () => {
      ipcRenderer.removeListener("godot:process", handler);
    };
  },
};

contextBridge.exposeInMainWorld("godot", bridge);
