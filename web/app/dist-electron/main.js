import { BrowserWindow, app, ipcMain } from "electron";
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
//#region ../packages/godot-rpc/src/codec.ts
/** JSON-RPC 2.0 标准错误码 + 业务错误码。 */
const RPC_ERROR = {
	PARSE_ERROR: -32700,
	INVALID_REQUEST: -32600,
	METHOD_NOT_FOUND: -32601,
	INVALID_PARAMS: -32602,
	INTERNAL_ERROR: -32603,
	/** 业务失败统一码：handler 返回 { ok:false, error } 时映射（内部字符串码入 error.data.code）。 */
	BIZ_ERROR: -32e3
};
function toRpcError(code, message, data) {
	return {
		code,
		message,
		...data === void 0 ? {} : { data }
	};
}
/**
* 解析一帧文本为 JSON-RPC 消息。严格性：
* - 非法 JSON → parse error（id null）；
* - 数组（batch）/非对象/非 2.0/响应歧义 → invalid request（合同显式拒绝 batch）；
* - request id 必须为 string（合同）；response id 必须为 string|null 且恰含 result 或 error。
*/
function decodeFrame(text) {
	let parsed;
	try {
		parsed = JSON.parse(text);
	} catch {
		return {
			kind: "error",
			error: toRpcError(RPC_ERROR.PARSE_ERROR, "Parse error"),
			id: null
		};
	}
	if (Array.isArray(parsed)) return {
		kind: "error",
		error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: batch 显式拒绝"),
		id: null
	};
	if (typeof parsed !== "object" || parsed === null) return {
		kind: "error",
		error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request"),
		id: null
	};
	const obj = parsed;
	if (obj.jsonrpc !== "2.0") return {
		kind: "error",
		error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: jsonrpc 必须为 \"2.0\""),
		id: null
	};
	if (typeof obj.method === "string") {
		if (Object.hasOwn(obj, "id")) {
			if (typeof obj.id !== "string") return {
				kind: "error",
				error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: request id 必须为 string"),
				id: null
			};
			return {
				kind: "request",
				request: {
					jsonrpc: "2.0",
					id: obj.id,
					method: obj.method,
					...Object.hasOwn(obj, "params") ? { params: obj.params } : {}
				}
			};
		}
		return {
			kind: "notification",
			notification: {
				jsonrpc: "2.0",
				method: obj.method,
				...Object.hasOwn(obj, "params") ? { params: obj.params } : {}
			}
		};
	}
	if (!Object.hasOwn(obj, "id") || obj.id !== null && typeof obj.id !== "string") return {
		kind: "error",
		error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request"),
		id: null
	};
	const hasResult = Object.hasOwn(obj, "result");
	const hasError = Object.hasOwn(obj, "error");
	if (hasResult === hasError) return {
		kind: "error",
		error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: response 必须恰含 result 或 error"),
		id: null
	};
	if (hasError) {
		const e = obj.error;
		if (typeof e !== "object" || e === null || typeof e.code !== "number" || typeof e.message !== "string") return {
			kind: "error",
			error: toRpcError(RPC_ERROR.INVALID_REQUEST, "Invalid Request: error 对象非法"),
			id: null
		};
		return {
			kind: "response",
			response: {
				jsonrpc: "2.0",
				id: obj.id,
				error: e
			}
		};
	}
	return {
		kind: "response",
		response: {
			jsonrpc: "2.0",
			id: obj.id,
			result: obj.result
		}
	};
}
//#endregion
//#region ../packages/godot-rpc/src/client.ts
/**
* JSON-RPC client：string id 生成 + req_id 配对 + 超时 + 通知下行。
* 传输无关（send 回调 + handleFrame 喂帧）——ws/ipc/inproc 通道共用此配对层。
*/
/** 业务调用错误（服务端 error 响应时 reject；含 code/data）。 */
var RpcCallError = class extends Error {
	code;
	data;
	constructor(code, message, data) {
		super(message);
		this.name = "RpcCallError";
		this.code = code;
		this.data = data;
	}
};
/** 本地超时（悬空防护）。 */
var RpcTimeoutError = class extends Error {
	constructor(method, timeoutMs) {
		super(`invoke 超时（${timeoutMs}ms）: ${method}`);
		this.name = "RpcTimeoutError";
	}
};
/** JSON-RPC client：id 生成 + 配对 + 超时 + 迟到应答丢弃 + 通知下行。 */
var RpcClient = class {
	send;
	seq = 0;
	pending = /* @__PURE__ */ new Map();
	notificationListeners = /* @__PURE__ */ new Set();
	defaultTimeoutMs;
	disposed = false;
	constructor(send, options = {}) {
		this.send = send;
		this.defaultTimeoutMs = options.defaultTimeoutMs ?? 1e4;
	}
	/** 发起调用：登记配对 → 发送帧。超时 reject RpcTimeoutError；业务/协议错误 reject RpcCallError。 */
	invoke(method, params, timeoutMs) {
		if (this.disposed) return Promise.reject(/* @__PURE__ */ new Error("RpcClient 已 dispose"));
		const effectiveTimeout = timeoutMs ?? this.defaultTimeoutMs;
		const id = `rpc_${++this.seq}`;
		const request = {
			jsonrpc: "2.0",
			id,
			method,
			...params === void 0 ? {} : { params }
		};
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => {
				this.pending.delete(id);
				reject(new RpcTimeoutError(method, effectiveTimeout));
			}, effectiveTimeout);
			this.pending.set(id, {
				resolve,
				reject,
				timer
			});
			this.send(JSON.stringify(request));
		});
	}
	/** 处理服务端下行帧：响应按 id 配对；通知转发监听器；未知 id 丢弃（迟到应答）。 */
	handleFrame(text) {
		if (this.disposed) return;
		const decoded = decodeFrame(text);
		switch (decoded.kind) {
			case "response": {
				const id = decoded.response.id;
				if (typeof id !== "string") return;
				const pending = this.pending.get(id);
				if (!pending) return;
				clearTimeout(pending.timer);
				this.pending.delete(id);
				if ("error" in decoded.response) pending.reject(new RpcCallError(decoded.response.error.code, decoded.response.error.message, decoded.response.error.data));
				else pending.resolve(decoded.response.result);
				return;
			}
			case "notification":
				for (const listener of this.notificationListeners) listener(decoded.notification.method, decoded.notification.params);
				return;
			case "request": return;
			case "error": return;
		}
	}
	/** 订阅服务端下行通知（事件下行）。返回退订函数。 */
	onNotification(listener) {
		this.notificationListeners.add(listener);
		return () => {
			this.notificationListeners.delete(listener);
		};
	}
	/** 断线清理：以稳定错误拒绝全部 pending。 */
	failAllPending(reason) {
		for (const [id, pending] of this.pending) {
			clearTimeout(pending.timer);
			pending.reject(new Error(reason));
			this.pending.delete(id);
		}
	}
	/** 当前挂起调用数（测试断言）。 */
	pendingCount() {
		return this.pending.size;
	}
	dispose() {
		this.disposed = true;
		this.failAllPending("RpcClient 已 dispose");
		this.notificationListeners.clear();
	}
};
//#endregion
//#region ../packages/godot-rpc/src/ws.ts
/**
* WS transport：WebSocket 通道 + RpcClient 配对 → Transport。
* 使用标准 WebSocket API（lib.dom 类型）——浏览器与 Node 22+ 原生 WebSocket 均可用，零运行时依赖。
*
* 重连策略：断开后按 backoffSeconds 递增退避重连，超过 maxReconnects 或 close() 后停止。
*/
const DEFAULT_BACKOFF_SECONDS = [
	.5,
	1,
	2,
	4,
	8
];
const DEFAULT_MAX_RECONNECTS$1 = 5;
function createWsTransport(options) {
	const backoffSeconds = options.backoffSeconds ?? DEFAULT_BACKOFF_SECONDS;
	const maxReconnects = options.maxReconnects ?? DEFAULT_MAX_RECONNECTS$1;
	const log = options.log ?? (() => {});
	let ws = null;
	let rpc = null;
	let state = "idle";
	let closed = false;
	let reconnectCount = 0;
	let reconnectTimer = null;
	let generation = 0;
	const eventListeners = /* @__PURE__ */ new Set();
	const requestQueue = [];
	function flushQueue() {
		for (const q of requestQueue.splice(0)) if (rpc) rpc.invoke(q.method, q.params, q.timeoutMs).then(q.resolve, q.reject);
		else q.reject(/* @__PURE__ */ new Error("连接已关闭，排队请求清空"));
	}
	function rejectQueue(reason) {
		for (const q of requestQueue.splice(0)) {
			clearTimeout(q.timer);
			q.reject(new Error(reason));
		}
	}
	function bindEventListeners() {
		for (const listener of eventListeners) rpc?.onNotification(listener);
	}
	function setState(next) {
		if (state !== next) {
			state = next;
			options.onStateChange?.(next);
		}
	}
	function failPending(reason) {
		if (rpc) rpc.failAllPending(reason);
	}
	function scheduleReconnect() {
		if (closed || reconnectCount >= maxReconnects) {
			setState("failed");
			return;
		}
		const delayMs = Math.min(backoffSeconds[reconnectCount] ?? backoffSeconds[backoffSeconds.length - 1], 60) * 1e3;
		reconnectCount++;
		setState("reconnecting");
		reconnectTimer = setTimeout(() => {
			reconnectTimer = null;
			connect();
		}, delayMs);
	}
	function connect() {
		if (closed) return;
		const gen = ++generation;
		setState("connecting");
		let socket;
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
			if (gen !== generation || closed) return;
			rpc = new RpcClient((text) => socket.send(text));
			bindEventListeners();
			flushQueue();
			setState("connected");
		};
		socket.onmessage = (ev) => {
			if (gen !== generation || closed) return;
			if (typeof ev.data === "string") rpc?.handleFrame(ev.data);
		};
		socket.onclose = () => {
			if (gen !== generation || closed) return;
			failPending("连接关闭");
			rejectQueue("连接关闭");
			rpc = null;
			scheduleReconnect();
		};
		socket.onerror = () => {
			log("[ws] 连接错误");
		};
	}
	connect();
	return {
		request(method, params, timeoutMs) {
			if (closed) return Promise.reject(/* @__PURE__ */ new Error("传输已关闭"));
			if (rpc) return rpc.invoke(method, params, timeoutMs);
			return new Promise((resolve, reject) => {
				const timer = setTimeout(() => {
					const idx = requestQueue.findIndex((q) => q.timer === timer);
					if (idx >= 0) {
						requestQueue.splice(idx, 1);
						reject(/* @__PURE__ */ new Error(`排队请求超时（${timeoutMs ?? 1e4}ms）: ${method}`));
					}
				}, timeoutMs ?? 1e4);
				requestQueue.push({
					method,
					params,
					timeoutMs,
					resolve,
					reject,
					timer
				});
			});
		},
		onEvent(listener) {
			eventListeners.add(listener);
			rpc?.onNotification(listener);
			return () => {
				eventListeners.delete(listener);
			};
		},
		resetReconnectBudget() {
			reconnectCount = 0;
		},
		close() {
			closed = true;
			if (reconnectTimer) {
				clearTimeout(reconnectTimer);
				reconnectTimer = null;
			}
			rejectQueue("传输已关闭");
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
		}
	};
}
//#endregion
//#region ../packages/godot-process/src/godot-client.ts
/** 认证 deadline（Provider 侧 3s）。 */
const DEFAULT_HELLO_TIMEOUT_MS = 3e3;
/** 默认重连上限（不含首次连接）。 */
const DEFAULT_MAX_RECONNECTS = 10;
var GodotClient = class {
	transport;
	token;
	helloTimeoutMs;
	onReady;
	projectPath;
	state_ = "idle";
	/** 已认证（握手成功）标记：invoke 门禁。 */
	ready = false;
	/** 成功握手次数（每次成功连接 +1）。 */
	epoch_ = 0;
	disposed = false;
	constructor(options = {}) {
		const url = options.url ?? process.env.BAIZE_GODOT_WS_URL ?? "";
		this.token = options.token ?? process.env.BAIZE_GODOT_TOKEN ?? "";
		if (url === "") throw new Error("缺少 BAIZE_GODOT_WS_URL：GodotClient 需要 Godot WS 地址");
		if (this.token === "") console.warn("[godot] BAIZE_GODOT_TOKEN 未设置——dev 宽松模式（hello 不携带有效 token）");
		this.helloTimeoutMs = options.helloTimeoutMs ?? DEFAULT_HELLO_TIMEOUT_MS;
		this.onReady = options.onReady;
		this.projectPath = options.projectPath;
		this.transport = createWsTransport({
			url,
			backoffSeconds: options.backoffSeconds,
			maxReconnects: options.maxReconnects ?? DEFAULT_MAX_RECONNECTS,
			onStateChange: (state) => this.handleTransportState(state),
			log: (msg) => console.log(`[godot] ${msg}`)
		});
		this.transport.onEvent((method) => {
			if (method === "shutdown" && !this.disposed) {
				console.log("[godot] 收到 shutdown 通知，停止连接循环");
				this.dispose();
			}
		});
	}
	/** 启动连接循环（幂等）：传输层在创建时已开始连接，本方法仅重置状态供 failed 后重试。 */
	connect() {
		if (this.disposed || this.state_ !== "idle" && this.state_ !== "failed") return;
		console.log(`[godot] 连接 Godot WS`);
		this.state_ = "connecting";
	}
	/** 代理能力调用；仅在已认证（ready）时放行，未就绪态确定性拒绝。 */
	invoke(method, params, timeoutMs) {
		if (this.disposed) return Promise.reject(/* @__PURE__ */ new Error("GodotClient 已 dispose"));
		if (!this.ready) {
			if (this.state_ === "failed") return Promise.reject(/* @__PURE__ */ new Error("GodotClient 连接失败（重连达上限），不可调用"));
			return Promise.reject(/* @__PURE__ */ new Error(`传输未认证（state=${this.state_}）`));
		}
		return this.transport.request(method, params, timeoutMs);
	}
	/** 停止重连 + 关闭连接 + 拒绝全部 pending（幂等）。 */
	dispose() {
		if (this.disposed) return;
		this.disposed = true;
		this.ready = false;
		this.state_ = "disposed";
		this.transport.close();
	}
	/** 订阅 Provider 下行事件（editor.selection_changed 等）。返回退订函数。 */
	onEvent(listener) {
		return this.transport.onEvent(listener);
	}
	get epoch() {
		return this.epoch_;
	}
	get state() {
		return this.state_;
	}
	get isConnected() {
		return this.ready;
	}
	/** 传输层状态 → 本类状态机；connected 时触发认证握手。 */
	handleTransportState(state) {
		if (this.disposed) return;
		switch (state) {
			case "connecting":
				this.state_ = "connecting";
				this.ready = false;
				break;
			case "connected":
				this.state_ = "connected";
				this.doHello();
				break;
			case "reconnecting":
				this.state_ = "reconnecting";
				this.ready = false;
				break;
			case "failed":
				this.state_ = "failed";
				this.ready = false;
				break;
			case "closed":
				this.state_ = "disposed";
				this.ready = false;
		}
	}
	/** 认证握手：成功 → ready + epoch+1 + onReady；失败 → 日志（传输层负责重连）。 */
	async doHello() {
		if (this.disposed) return;
		try {
			const hello = await this.transport.request("hello", { token: this.token }, this.helloTimeoutMs);
			if (this.disposed) return;
			if (hello.ok !== true) {
				console.error("[godot] 握手失败: hello 返回 ok=false");
				return;
			}
			this.epoch_ += 1;
			this.ready = true;
			this.state_ = "connected";
			this.transport.resetReconnectBudget?.();
			console.log(`[godot] Godot WS 已认证（epoch=${this.epoch_}）`);
			if (this.projectPath !== void 0) console.log(`[godot] 项目路径: ${this.projectPath}`);
			this.onReady?.(this, hello);
		} catch (err) {
			if (this.disposed) return;
			if (err instanceof RpcCallError) {
				const dataCode = err.data?.code;
				console.error(`[godot] 握手失败: hello 被拒绝（code=${err.code}${dataCode !== void 0 ? `, data.code=${dataCode}` : ""}）——请核对 token`);
			} else if (err instanceof RpcTimeoutError) console.error(`[godot] 握手超时（${this.helloTimeoutMs}ms）: hello 无应答——Godot 侧未就绪？`);
			else console.error("[godot] 握手失败:", err instanceof Error ? err.message : String(err));
		}
	}
};
//#endregion
//#region electron/main.ts
/**
* Electron 主进程：窗口管理 + Godot 进程生命周期 + IPC 桥。
* 编译：tsc（TS7）→ dist-electron/main.js（CommonJS，Electron 主进程兼容）。
*
* 职责边界（架构 §3.0）：
* - godot-process（GodotClient）：spawn Godot / WS 连接 / 认证 / 生命周期；
* - 本文件：BrowserWindow + IPC（渲染进程请求 → GodotClient → Provider）+ 事件下行转发；
* - 渲染进程不直连 WS/token（安全模型：经主进程转发）。
*/
const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../../../");
const GODOT_EXE = resolve(REPO_ROOT, process.platform === "win32" ? "bin/godot.windows.editor.dev.x86_64.console.exe" : "bin/godot.macos.editor.dev.arm64");
const DEFAULT_PROJECT = resolve(REPO_ROOT, "test-projects/provider");
const PROVIDER_URL = "ws://127.0.0.1:23009";
const RENDERER_DEV_URL = process.env.VITE_DEV_SERVER_URL ?? "http://localhost:5173";
let client = null;
let godotChild = null;
let mainWindow = null;
function log(msg) {
	console.log(`[app:main] ${msg}`);
}
function startGodot() {
	if (!existsSync(GODOT_EXE)) {
		console.error(`[app:main] Godot 编辑器不存在: ${GODOT_EXE}\n请先执行 task dev 构建。`);
		return;
	}
	const project = process.env.BAIZE_PROJECT_PATH ?? DEFAULT_PROJECT;
	log(`spawn Godot: ${GODOT_EXE} --path ${project} --editor --headless`);
	godotChild = spawn(GODOT_EXE, [
		"--path",
		project,
		"--editor",
		"--headless"
	], {
		stdio: [
			"ignore",
			"pipe",
			"pipe"
		],
		windowsHide: true
	});
	godotChild.stdout?.on("data", (d) => process.stdout.write(`[godot] ${d}`));
	godotChild.stderr?.on("data", (d) => process.stderr.write(`[godot:err] ${d}`));
	godotChild.on("exit", (code) => {
		log(`Godot 进程退出（code=${code}）`);
		godotChild = null;
	});
}
function setupIpc() {
	ipcMain.handle("godot:request", async (_e, method, params) => {
		if (!client) throw new Error("Godot 未连接");
		return client.invoke(method, params);
	});
	client?.onEvent((method, params) => {
		mainWindow?.webContents.send("godot:event", {
			method,
			params
		});
	});
}
function createWindow() {
	mainWindow = new BrowserWindow({
		width: 1280,
		height: 800,
		title: "Baize Editor",
		webPreferences: {
			preload: join(dirname(fileURLToPath(import.meta.url)), "preload.cjs"),
			contextIsolation: true,
			nodeIntegration: false,
			sandbox: true
		}
	});
	if (process.env.NODE_ENV === "development") mainWindow.loadURL(RENDERER_DEV_URL);
	else mainWindow.loadFile(join(dirname(fileURLToPath(import.meta.url)), "../dist/index.html"));
	mainWindow.webContents.on("console-message", (_e, level, message, line, sourceId) => {
		console.log();
	});
	mainWindow.webContents.on("did-fail-load", (_e, code, desc, url) => {
		console.error();
	});
	mainWindow.on("closed", () => {
		mainWindow = null;
	});
}
app.whenReady().then(async () => {
	startGodot();
	client = new GodotClient({
		url: PROVIDER_URL,
		token: ""
	});
	setupIpc();
	createWindow();
	app.on("activate", () => {
		if (BrowserWindow.getAllWindows().length === 0) createWindow();
	});
});
app.on("window-all-closed", () => {
	if (process.platform !== "darwin") app.quit();
});
app.on("before-quit", () => {
	client?.dispose();
	client = null;
	godotChild?.kill();
	godotChild = null;
});
//#endregion
export {};

//# sourceMappingURL=main.js.map