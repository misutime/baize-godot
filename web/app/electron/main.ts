/**
 * Electron 主进程：窗口管理 + Godot 进程生命周期 + IPC 桥。
 * 编译：tsc（TS7）→ dist-electron/main.js（CommonJS，Electron 主进程兼容）。
 *
 * 职责边界（架构 §3.0）：
 * - godot-process（GodotClient）：spawn Godot / WS 连接 / 认证 / 生命周期；
 * - 本文件：BrowserWindow + IPC（渲染进程请求 → GodotClient → Provider）+ 事件下行转发；
 * - 渲染进程不直连 WS/token（安全模型：经主进程转发）。
 */
import { app, BrowserWindow, ipcMain } from "electron";
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { GodotClient } from "@baize/godot-process";

// dist-electron/ → web/app → web → 仓库根
const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../../../");
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
// 渲染进程 dev server（vite）端口；prod 用 dist/ 产物。与 vite.config server.host 保持一致（127.0.0.1）。
const RENDERER_DEV_URL = process.env.VITE_DEV_SERVER_URL ?? "http://127.0.0.1:5173";

let client: GodotClient | null = null;
let godotChild: ReturnType<typeof spawn> | null = null;
let mainWindow: BrowserWindow | null = null;
let quitting = false; // before-quit 编排中：不触发 respawn
// 最近一次进程状态（缓存）：渲染进程晚订阅/窗口重建（macOS activate）/reload 后重放，避免面板永久"连接中"
let lastGodotStatus: {
  state: "starting" | "running" | "exited" | "error" | "restarting";
  code?: number | null;
  provider: "connecting" | "connected" | "disconnected";
} | null = null;

function log(msg: string): void {
  console.log(`[app:main] ${msg}`);
}

/** 下行 Godot 进程/连接状态给渲染进程（视口面板数据源）；缓存供晚订阅/新窗口重放。 */
function broadcastGodotStatus(payload: {
  state: "starting" | "running" | "exited" | "error" | "restarting";
  code?: number | null;
  provider: "connecting" | "connected" | "disconnected";
}): void {
  lastGodotStatus = payload;
  mainWindow?.webContents.send("godot:process", payload);
}

/** 创建 GodotClient（WS 连接 + 事件转发）。构造即开始连接（createWsTransport 内部启动）。 */
function createClient(): void {
  client = new GodotClient({
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
  client.onEvent((method, params) => {
    mainWindow?.webContents.send("godot:event", { method, params });
  });
}

/** Godot 异常（退出/spawn 失败）后的受控重启（非退出编排时；review P3）。 */
function scheduleGodotRestart(delayMs: number): void {
  if (quitting) {
    return;
  }
  setTimeout(() => {
    if (!quitting && !godotChild) {
      log("Godot 异常，自动重启…");
      broadcastGodotStatus({ state: "restarting", provider: "disconnected" });
      client?.dispose(); // shutdown 通知后旧实例已 disposed 不可重连——必须重建（review）
      client = null;
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
  godotChild = spawn(GODOT_EXE, ["--path", project, "--editor", "--resolution", "1024x768"], {
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  broadcastGodotStatus({ state: "running", provider: "connecting" });
  godotChild.stdout?.on("data", (d) => process.stdout.write(`[godot] ${d}`));
  godotChild.stderr?.on("data", (d) => process.stderr.write(`[godot:err] ${d}`));
  godotChild.on("exit", (code) => {
    log(`Godot 进程退出（code=${code}）`);
    godotChild = null;
    broadcastGodotStatus({ state: "exited", code, provider: "disconnected" });
    scheduleGodotRestart(1000); // 异常退出：1s 后受控重启（崩溃/误关窗口）
  });
  godotChild.on("error", (err) => {
    // spawn 失败（exe 被删/无权限等）：记录并清理，不触发 uncaught error 退出主进程（review）
    console.error(`[app:main] Godot spawn 失败: ${err.message}`);
    godotChild = null;
    broadcastGodotStatus({ state: "error", provider: "disconnected" });
    scheduleGodotRestart(5000); // 配置类问题：5s 后重试（构建中 exe 临时锁等可恢复）
  });
}

function setupIpc(): void {
  // 渲染进程 → Provider：经主进程转发（token/端口不出渲染进程）
  // 方法白名单：只允许能力面命名空间（review P2：防任意方法调用）
  const ALLOWED_METHOD_PREFIXES = ["scene.", "editor."];
  ipcMain.handle("godot:request", async (e, method: string, params: unknown) => {
    if (!ALLOWED_METHOD_PREFIXES.some((p) => typeof method === "string" && method.startsWith(p))) {
      throw new Error(`方法不在白名单: ${String(method)}`);
    }
    if (e.sender !== mainWindow?.webContents) {
      throw new Error("拒绝非主窗口的请求"); // sender 身份校验（review 安全）
    }
    if (!client) {
      throw new Error("Godot 未连接");
    }
    try {
      return { ok: true, result: await client.invoke(method, params) };
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

  // Provider 事件 → 渲染进程：下行转发（createClient 中绑定；此处不再重复）
}

function createWindow(): void {
  mainWindow = new BrowserWindow({
    width: 1280,
    height: 800,
    title: "Baize Editor",
    webPreferences: {
      preload: join(dirname(fileURLToPath(import.meta.url)), "preload.cjs"),
      contextIsolation: true, // 安全默认：渲染进程与 preload 隔离
      nodeIntegration: false, // 渲染进程无 Node
      sandbox: true,
    },
  });

  // 调试期默认打开右侧 DevTools（开发面板；正式发布时移除或加开关）
  mainWindow.webContents.openDevTools({ mode: "right" });

  if (process.env.NODE_ENV === "development") {
    mainWindow.loadURL(RENDERER_DEV_URL);
  } else {
    mainWindow.loadFile(join(dirname(fileURLToPath(import.meta.url)), "../dist/index.html"));
  }
  // 防导航：渲染进程只允许加载本应用页面（review 安全——阻止被导航到任意 URL）
  mainWindow.webContents.on("will-navigate", (event) => {
    event.preventDefault();
  });
  mainWindow.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  // 页面加载完成后重放最近进程状态（晚订阅/Reload/macOS 重建窗口后面板不永久"连接中"）
  mainWindow.webContents.on("did-finish-load", () => {
    if (lastGodotStatus) {
      mainWindow?.webContents.send("godot:process", lastGodotStatus);
    }
  });

  // 诊断：渲染进程 console/加载错误转发到主进程 stdout（GUI 无输出面板）
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
  // Provider 启动需要几秒（编辑器核心初始化）——GodotClient 带退避重连，无需显式等待
  createClient();
  setupIpc();
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});

app.on("before-quit", () => {
  quitting = true;
  // 退出编排：停止重连 + 关 WS（Godot 进程随 app 退出由 OS 回收；正式版由 godot-process 编排 shutdown）
  client?.dispose();
  client = null;
  godotChild?.kill();
  godotChild = null;
});
