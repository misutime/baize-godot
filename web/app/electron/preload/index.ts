/**
 * preload：contextBridge 暴露受控 API 给渲染进程。
 * 安全模型：渲染进程只拿到 { request, onEvent, onProcessStatus }——不暴露 ipcRenderer 本身、
 * 不暴露 token/端口；能力方法名与参数由主进程校验后转发。
 * 编译：vite-plugin-electron → dist-electron/preload/index.cjs（CommonJS，sandbox 兼容）。
 */
import { contextBridge, type IpcRendererEvent, ipcRenderer } from "electron";

import { type GodotBridge, type GodotProcessStatus, IPC } from "../../src/shared/ipc";

const bridge: GodotBridge = {
  request: async (method, params) => {
    // 主进程返回结构化 {ok, result|error}（Electron IPC 错误序列化会丢自定义字段）——
    // 解包：成功返回 result，失败还原 RpcCallError（带 code/data）。
    const res = (await ipcRenderer.invoke(IPC.request, method, params)) as
      | { ok: true; result: unknown }
      | { ok: false; error: { message: string; code?: number; data?: { code?: string } } };
    if (res.ok) {
      return res.result;
    }
    const err = new Error(res.error.message) as Error & { code?: number; data?: { code?: string } };
    err.code = res.error.code;
    err.data = res.error.data;
    throw err;
  },
  onEvent: (listener) => {
    const handler = (_e: IpcRendererEvent, ev: { method: string; params: unknown }): void => {
      listener(ev.method, ev.params);
    };
    ipcRenderer.on(IPC.event, handler);
    return () => {
      ipcRenderer.removeListener(IPC.event, handler);
    };
  },
  onProcessStatus: (listener) => {
    const handler = (_e: IpcRendererEvent, status: GodotProcessStatus): void => {
      listener(status);
    };
    ipcRenderer.on(IPC.process, handler);
    return () => {
      ipcRenderer.removeListener(IPC.process, handler);
    };
  },
  viewportRectChanged: (rect) => {
    ipcRenderer.send(IPC.viewportRect, rect);
  },
};

contextBridge.exposeInMainWorld("godot", bridge);
