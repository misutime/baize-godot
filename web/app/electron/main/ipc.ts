/**
 * IPC：渲染进程 → 主进程 → GodotClient 的转发桥。
 * 安全模型：token/端口不出渲染进程；方法白名单 + sender 身份校验。
 */
import { ipcMain } from "electron";

import { type ViewportRect, IPC } from "../../src/shared/ipc";
import { state } from "../state";
import { syncViewportRect } from "./godot";

export function setupIpc(): void {
  // 方法白名单：只允许能力面命名空间（review P2：防任意方法调用）
  const ALLOWED_METHOD_PREFIXES = ["scene.", "editor."];
  ipcMain.handle(IPC.request, async (e, method: string, params: unknown) => {
    if (!ALLOWED_METHOD_PREFIXES.some((p) => typeof method === "string" && method.startsWith(p))) {
      throw new Error(`方法不在白名单: ${String(method)}`);
    }
    if (e.sender !== state.mainWindow?.webContents) {
      throw new Error("拒绝非主窗口的请求"); // sender 身份校验（review 安全）
    }
    if (!state.client) {
      throw new Error("Godot 未连接");
    }
    try {
      return { ok: true, result: await state.client.invoke(method, params) };
    } catch (err) {
      // Electron IPC 只序列化 Error 的 message/name，自定义字段（code/data）会丢——
      // 显式结构化包装，渲染进程据此还原 RpcCallError（review：data.code 是错误契约一部分）。
      const e2 = err as { code?: number; data?: unknown; message?: string };
      return {
        ok: false,
        error: { message: e2.message ?? String(err), code: e2.code, data: e2.data },
      };
    }
  });

  // C-lite：渲染进程上报视口矩形（DIP，相对内容区）→ 缓存 + 同步（sender 校验同 request）。
  ipcMain.on(IPC.viewportRect, (e, rect: ViewportRect) => {
    if (e.sender !== state.mainWindow?.webContents) {
      return; // 拒绝非主窗口的上报
    }
    state.viewportRect = rect;
    syncViewportRect();
  });
}
