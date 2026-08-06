/**
 * Electron 主进程入口：生命周期 + 窗口管理 + 单实例。
 * 编译/打包：vite-plugin-electron（electronSimple）→ dist-electron/main/index.js（ESM）。
 */

import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { app, BrowserWindow } from "electron";

import { IPC } from "../../src/shared/ipc";
import { state } from "../state";
import { disposeGodot, initGodot } from "./godot";
import { setupIpc } from "./ipc";

// vite-plugin-electron dev 注入的渲染进程 dev server URL；未设置 = 生产构建（loadFile dist/）。
// 插件的 resolveServerUrl 会把回环地址（127.0.0.1/::1）映射为 localhost——vite server 固定绑定
// 127.0.0.1，这里强制规范回 IPv4，避免 localhost→::1（IPv6/Clash 环境）连接失败（review PR#3 P2）。
const VITE_DEV_SERVER_URL = process.env.VITE_DEV_SERVER_URL?.replace("localhost", "127.0.0.1") ?? null;

// 单实例：编辑器类应用多开无意义，第二实例唤醒主窗口（P1-4）。
if (!app.requestSingleInstanceLock()) {
  app.quit();
  process.exit(0);
}

// 官方模式：锁检查后立即注册（模块求值期同步注册，事件循环启动前已就位，无事件丢失竞态）。
app.on("second-instance", () => {
  if (state.mainWindow) {
    // 聚焦主窗口（最小化时先恢复）
    if (state.mainWindow.isMinimized()) {
      state.mainWindow.restore();
    }
    state.mainWindow.focus();
  }
});

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1280,
    height: 800,
    title: "Baize Editor",
    webPreferences: {
      preload: join(dirname(fileURLToPath(import.meta.url)), "../preload/index.cjs"),
      contextIsolation: true, // 安全默认：渲染进程与 preload 隔离
      nodeIntegration: false, // 渲染进程无 Node
      sandbox: true, // preload 必须为 CommonJS（dist-electron/preload/index.cjs）
    },
  });
  state.mainWindow = win;

  // 调试开关（P2-5）：仅 dev（dev server 存在）打开 DevTools；VSCODE_DEBUG=0 可显式关闭。
  if (VITE_DEV_SERVER_URL && process.env.VSCODE_DEBUG !== "0") {
    win.webContents.openDevTools({ mode: "right" });
  }

  if (VITE_DEV_SERVER_URL) {
    win.loadURL(VITE_DEV_SERVER_URL);
  } else {
    // dist-electron/main/index.js → dist-electron → app → dist/
    win.loadFile(join(dirname(fileURLToPath(import.meta.url)), "../../dist/index.html"));
  }
  // 防导航：渲染进程只允许加载本应用页面（review 安全——阻止被导航到任意 URL）
  win.webContents.on("will-navigate", (event) => {
    event.preventDefault();
  });
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  // 页面加载完成后重放最近进程状态（晚订阅/Reload/macOS 重建窗口后面板不永久"连接中"）
  win.webContents.on("did-finish-load", () => {
    if (state.lastGodotStatus) {
      win.webContents.send(IPC.process, state.lastGodotStatus);
    }
  });
  // 诊断：渲染进程 console/加载错误转发到主进程 stdout（GUI 无输出面板）
  win.webContents.on(
    "console-message",
    (details: Electron.Event<Electron.WebContentsConsoleMessageEventParams>) => {
      console.log(
        `[renderer:${details.level}] ${details.message} (${details.sourceId}:${details.lineNumber})`,
      );
    },
  );
  win.webContents.on("did-fail-load", (_e, code, desc, url) => {
    console.error(`[renderer] did-fail-load ${code} ${desc}: ${url}`);
  });
  win.on("closed", () => {
    state.mainWindow = null;
  });
}

app.whenReady().then(() => {
  initGodot();
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
  disposeGodot();
});
