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
const PROVIDER_URL = "ws://127.0.0.1:23009";
// 渲染进程 dev server（vite）端口；prod 用 dist/ 产物
const RENDERER_DEV_URL = process.env.VITE_DEV_SERVER_URL ?? "http://localhost:5173";

let client: GodotClient | null = null;
let godotChild: ReturnType<typeof spawn> | null = null;
let mainWindow: BrowserWindow | null = null;

function log(msg: string): void {
  console.log(`[app:main] ${msg}`);
}

function startGodot(): void {
  if (!existsSync(GODOT_EXE)) {
    console.error(`[app:main] Godot 编辑器不存在: ${GODOT_EXE}\n请先执行 task dev 构建。`);
    return;
  }
  const project = process.env.BAIZE_PROJECT_PATH ?? DEFAULT_PROJECT;
  log(`spawn Godot: ${GODOT_EXE} --path ${project} --editor --headless`);
  godotChild = spawn(GODOT_EXE, ["--path", project, "--editor", "--headless"], {
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  godotChild.stdout?.on("data", (d) => process.stdout.write(`[godot] ${d}`));
  godotChild.stderr?.on("data", (d) => process.stderr.write(`[godot:err] ${d}`));
  godotChild.on("exit", (code) => {
    log(`Godot 进程退出（code=${code}）`);
    godotChild = null;
  });
}

function setupIpc(): void {
  // 渲染进程 → Provider：经主进程转发（token/端口不出渲染进程）
  ipcMain.handle("godot:request", async (_e, method: string, params: unknown) => {
    if (!client) {
      throw new Error("Godot 未连接");
    }
    return client.invoke(method, params);
  });

  // Provider 事件 → 渲染进程：下行转发
  client?.onEvent((method, params) => {
    mainWindow?.webContents.send("godot:event", { method, params });
  });
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

  if (process.env.NODE_ENV === "development") {
    mainWindow.loadURL(RENDERER_DEV_URL);
  } else {
    mainWindow.loadFile(join(dirname(fileURLToPath(import.meta.url)), "../dist/index.html"));
  }
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
  client = new GodotClient({ url: PROVIDER_URL, token: "" });
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
  // 退出编排：停止重连 + 关 WS（Godot 进程随 app 退出由 OS 回收；正式版由 godot-process 编排 shutdown）
  client?.dispose();
  client = null;
  godotChild?.kill();
  godotChild = null;
});
