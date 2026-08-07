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

/** 创建 GodotClient（WS 连接 + 事件转发）。构造即开始连接（createWsTransport 内部启动）。 */
function createClient(): void {
  state.client = new GodotClient({
    url: PROVIDER_URL,
    token: PROVIDER_TOKEN,
    onReady: () => broadcastGodotStatus({ state: "running", provider: "connected" }),
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
  // Provider 事件 → 渲染进程：下行转发
  state.client.onEvent((method, params) => {
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
  // 视口策略 A：Godot 窗口模式（--editor）+ 默认视口尺寸（--resolution）；Electron 面板并列。
  log(`spawn Godot: ${GODOT_EXE} --path ${project} --editor --resolution 1024x768`);
  state.godotChild = spawn(GODOT_EXE, ["--path", project, "--editor", "--resolution", "1024x768"], {
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
