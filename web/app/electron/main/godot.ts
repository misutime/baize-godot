/**
 * Godot 进程生命周期：spawn / WS 连接（GodotClient）/ 认证 / 受控重启 / 状态下行。
 * 职责边界（架构 §3.0）：godot-process（GodotClient）负责连接与生命周期；
 * 本模块负责 spawn 编排与状态广播，渲染进程不直连 WS/token。
 *
 * 路径常量基于 dev 布局（仓库根 bin/ 产物）；打包分发时随 extraResources 布局重写（review P2-6）。
 */
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { screen } from "electron";
import { GodotClient } from "@baize/godot-process";

import { type GodotProcessStatus, IPC } from "../../src/shared/ipc";
import { state } from "../state";

// dist-electron/main/index.js → dist-electron → web/app → web → 仓库根
const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../");
const GODOT_EXE = resolve(
  REPO_ROOT,
  process.platform === "win32"
    ? "bin/godot.windows.editor.dev.x86_64.console.exe"
    : "bin/godot.macos.editor.dev.arm64",
);
const DEFAULT_PROJECT = resolve(REPO_ROOT, "test-projects/provider");
// 端口/token 与 Provider 同源（env；缺省 dev 宽松）——review P2
const PROVIDER_PORT = process.env.BAIZE_PROVIDER_PORT ?? "23009";
const PROVIDER_URL = `ws://127.0.0.1:${PROVIDER_PORT}`;
const PROVIDER_TOKEN = process.env.BAIZE_PROVIDER_TOKEN ?? "";

function log(msg: string): void {
  console.log(`[app:main] ${msg}`);
}

/** 下行 Godot 进程/连接状态给渲染进程（视口面板数据源）；缓存供晚订阅/新窗口重放。 */
function broadcastGodotStatus(payload: GodotProcessStatus): void {
  state.lastGodotStatus = payload;
  state.mainWindow?.webContents.send(IPC.process, payload);
}

/** 启动期焦点保护解除（幂等）：Godot 嵌入窗口恢复可激活 + Electron 窗口恢复可聚焦。
 * 触发：editor.ready 事件（精确信号）；兜底：onReady 后 5s 定时器（事件丢失时）。
 * 注：editor.ready 通知可能先于客户端认证完成到达，set_no_focus 失败则退避重试（最多 5 次）。 */
let startupProtectionReleased = false;
function releaseStartupProtection(): void {
	if (startupProtectionReleased) {
		return;
	}
	startupProtectionReleased = true;
	state.mainWindow?.setFocusable(true);
	log("C-lite 启动保护解除（Electron 窗口可聚焦）");
	let attempts = 0;
	const tryReleaseNoFocus = (): void => {
		attempts++;
		state.client
			?.invoke("viewport.set_no_focus", { enabled: false })
			.catch((err: unknown) => {
				if (attempts < 5) {
					setTimeout(tryReleaseNoFocus, 1000);
				} else {
					log(`viewport.set_no_focus 重试耗尽: ${(err as Error)?.message ?? String(err)}`);
				}
			});
	};
	tryReleaseNoFocus();
}

/** 创建 GodotClient（WS 连接 + 事件转发）。构造即开始连接（createWsTransport 内部启动）。 */
function createClient(): void {
  state.client = new GodotClient({
    url: PROVIDER_URL,
    token: PROVIDER_TOKEN,
    onReady: () => {
        broadcastGodotStatus({ state: "running", provider: "connected" });
        // C-lite：连接建立后补发视口矩形（renderer 首次上报常早于 WS 认证而失败，此处重同步）。
		syncViewportRect();
		// C-lite：偏移（相对宿主窗口原点）同步——布局数据源，仅随布局变化；位移跟随由 Godot 每帧处理。
		syncViewportOffset();
		// C-lite：启动保护由 editor.ready 事件精确解除（见 releaseStartupProtection）；此处仅兜底（事件丢失）。
        setTimeout(releaseStartupProtection, 5000);
    },
    // WS 断连/重连中（Godot 进程仍在）也要下行 provider 状态（review：面板不能谎报"已连接"）；
    // 注意：transport "connected" = WS 已打开但认证（hello）尚未完成——映射为 connecting，
    // 认证成功（onReady）才报 connected（review：token 错误时不得短暂谎报）。
    onStateChange: (s) =>
      broadcastGodotStatus({
        state: "running",
        provider:
          s === "connecting" || s === "reconnecting" || s === "connected"
            ? "connecting" // connected = WS 已开但认证未完成（onReady 才报已连接）
            : "disconnected",
      }),
  });
  // Provider 事件 → 渲染进程：下行转发；editor.ready → 解除启动期焦点保护（精确信号）。
  state.client.onEvent((method, params) => {
    if (method === "editor.ready") {
      releaseStartupProtection();
    }
    state.mainWindow?.webContents.send(IPC.event, { method, params });
  });
}

/** Godot 异常（退出/spawn 失败）后的受控重启（非退出编排时；review P3）。 */
function scheduleGodotRestart(delayMs: number): void {
  if (state.quitting) {
    return;
  }
  setTimeout(() => {
    if (!state.quitting && !state.godotChild) {
      log("Godot 异常，自动重启…");
      broadcastGodotStatus({ state: "restarting", provider: "disconnected" });
      state.client?.dispose(); // shutdown 通知后旧实例已 disposed 不可重连——必须重建（review）
      state.client = null;
      startGodot();
      createClient();
    }
  }, delayMs);
}

function startGodot(): void {
  if (!existsSync(GODOT_EXE)) {
    console.error(`[app:main] Godot 编辑器不存在: ${GODOT_EXE}\n请先执行 task dev 构建。`);
    // 早退路径也要下行状态（否则面板停留在"连接中"且无任何提示）
    broadcastGodotStatus({ state: "error", provider: "disconnected" });
    scheduleGodotRestart(5000); // exe 尚未产出（构建中）：持续重试直到可用（review）
    return;
  }
  const project = process.env.BAIZE_PROJECT_PATH ?? DEFAULT_PROJECT;
  const args = ["--path", project, "--editor"];
  const hwnd_buf = state.mainWindow?.getNativeWindowHandle();
  const wid = hwnd_buf && hwnd_buf.length >= 8 ? Number(hwnd_buf.readBigUInt64LE(0)) : 0;
  if (wid) {
    // C-lite：Godot 主窗口作为 Electron 主窗口的 owned window（上游 --wid 机制，spawn 时挂接）。
    args.push("--wid", String(wid));
    // C-lite 启动期防焦点死锁（splash 期点击 owner 会触发双方冻结）：嵌入窗口初始 no-focus，
    // 编辑器就绪后经 viewport.set_no_focus(false) 解除（onReady 延迟，见 createClient）。
    args.push("--embedded-no-focus");
  } else {
    // 兜底（窗口尚未创建/非 Win）：策略 A 独立窗口形态。
    args.push("--resolution", "1024x768");
  }
  log(`spawn Godot: ${GODOT_EXE} ${args.join(" ")}`);
  state.godotChild = spawn(GODOT_EXE, args, {
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  broadcastGodotStatus({ state: "running", provider: "connecting" });
  state.godotChild.stdout?.on("data", (d) => process.stdout.write(`[godot] ${d}`));
  state.godotChild.stderr?.on("data", (d) => process.stderr.write(`[godot:err] ${d}`));
  state.godotChild.on("exit", (code) => {
    log(`Godot 进程退出（code=${code}）`);
    state.godotChild = null;
    broadcastGodotStatus({ state: "exited", code, provider: "disconnected" });
    scheduleGodotRestart(1000); // 异常退出：1s 后受控重启（崩溃/误关窗口）
  });
  state.godotChild.on("error", (err) => {
    // spawn 失败（exe 被删/无权限等）：记录并清理，不触发 uncaught error 退出主进程（review）
    console.error(`[app:main] Godot spawn 失败: ${err.message}`);
    state.godotChild = null;
    broadcastGodotStatus({ state: "error", provider: "disconnected" });
    scheduleGodotRestart(5000); // 配置类问题：5s 后重试（构建中 exe 临时锁等可恢复）
  });
}

/** will-move/will-resize 事件的新 bounds 是窗口外框；换算为内容区（减去当前外框↔内容区差值）。 */
function contentBoundsFromWindow(p_bounds: Electron.Rectangle): Electron.Rectangle {
	const win = state.mainWindow;
	if (!win) {
		return p_bounds;
	}
	const wb = win.getBounds();
	const cb = win.getContentBounds();
	return {
		x: p_bounds.x + (cb.x - wb.x),
		y: p_bounds.y + (cb.y - wb.y),
		width: p_bounds.width + (cb.width - wb.width),
		height: p_bounds.height + (cb.height - wb.height),
	};
}

/**
 * C-lite 视口几何同步：把渲染进程上报的视口矩形（DIP、相对内容区）换算为
 * Godot 屏幕坐标空间并下发 viewport.set_window_rect。
 * 坐标契约：Godot window_set_position 输入 = Win32 物理像素 − 虚拟屏幕原点；
 * 当前实现假设单显示器（原点 0,0），多屏/DPI 换算在 W6 验收。
 * @param p_window_bounds will-move/will-resize 传入的新窗口 bounds（提前摆位，同帧到达）；缺省用当前窗口几何。
 */
export function syncViewportRect(p_window_bounds?: Electron.Rectangle): void {
	const win = state.mainWindow;
	const client = state.client;
	if (!win || !client || !state.viewportRect) {
		return;
	}
	const content = p_window_bounds ? contentBoundsFromWindow(p_window_bounds) : win.getContentBounds();
	const scale = screen.getDisplayMatching(win.getBounds()).scaleFactor;
	const r = state.viewportRect;
	const x = Math.round((content.x + r.x) * scale);
	const y = Math.round((content.y + r.y) * scale);
	const w = Math.max(Math.round(r.w * scale), 1);
	const h = Math.max(Math.round(r.h * scale), 1);
	client
		.invoke("viewport.set_window_rect", { x, y, w, h })
		.catch((err: unknown) => {
			log(`viewport.set_window_rect 失败: ${(err as Error)?.message ?? String(err)}`);
		});
}

/** C-lite 视口偏移同步：视口矩形相对宿主窗口原点（DIP 内容区 → 窗口原点 → 物理像素）→ viewport.set_viewport_offset。
 * 位置由 Godot 每帧按 owner+offset 重组（跟随）独占；此偏移仅随布局变化更新（renderer 数据，天然新鲜）。 */
export function syncViewportOffset(): void {
	const win = state.mainWindow;
	const client = state.client;
	if (!win || !client || !state.viewportRect) {
		return;
	}
	const wb = win.getBounds();
	const cb = win.getContentBounds();
	const scale = screen.getDisplayMatching(win.getBounds()).scaleFactor;
	const r = state.viewportRect;
	const ox = Math.round(((cb.x - wb.x) + r.x) * scale);
	const oy = Math.round(((cb.y - wb.y) + r.y) * scale);
	client
		.invoke("viewport.set_viewport_offset", { x: ox, y: oy })
		.catch((err: unknown) => {
			log(`viewport.set_viewport_offset 失败: ${(err as Error)?.message ?? String(err)}`);
		});
}

/** 启动 Godot 进程与 WS 客户端（app ready 后调用一次）。 */
export function initGodot(): void {
	startGodot();
  // Provider 启动需要几秒（编辑器核心初始化）——GodotClient 带退避重连，无需显式等待
  createClient();
}

/** 退出编排：停止重连 + 关 WS（Godot 进程随 app 退出由 OS 回收；正式版由 godot-process 编排 shutdown）。 */
export function disposeGodot(): void {
  state.quitting = true;
  state.client?.dispose();
  state.client = null;
  state.godotChild?.kill();
  state.godotChild = null;
}
