let electron = require("electron");
//#region electron/preload.ts
/**
* preload：contextBridge 暴露受控 API 给渲染进程。
* 安全模型：渲染进程只拿到 { request, onEvent }——不暴露 ipcRenderer 本身、
* 不暴露 token/端口；能力方法名与参数由主进程校验后转发。
* 编译：tsc（TS7）→ dist-electron/preload.js（CommonJS，sandbox 兼容）。
*/
electron.contextBridge.exposeInMainWorld("godot", {
	request: (method, params) => electron.ipcRenderer.invoke("godot:request", method, params),
	onEvent: (listener) => {
		const handler = (_e, ev) => {
			listener(ev.method, ev.params);
		};
		electron.ipcRenderer.on("godot:event", handler);
		return () => {
			electron.ipcRenderer.removeListener("godot:event", handler);
		};
	}
});
//#endregion

//# sourceMappingURL=preload.cjs.map