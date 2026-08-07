/**
 * electron/main/ 各模块共享的可变状态（模块间唯一状态源）。
 * 拆分 godot.ts / ipc.ts / index.ts 后避免跨文件传参与循环依赖。
 */

import type { ChildProcess } from "node:child_process";
import type { GodotClient } from "@baize/godot-process";
import type { BrowserWindow } from "electron";
import type { GodotProcessStatus, ViewportRect } from "../src/shared/ipc";

export const state = {
  client: null as GodotClient | null,
  godotChild: null as ChildProcess | null,
  mainWindow: null as BrowserWindow | null,
  /** before-quit 编排中：不再触发 respawn。 */
  quitting: false,
  /** 最近一次进程状态（缓存）：渲染进程晚订阅/窗口重建/reload 后重放。 */
  lastGodotStatus: null as GodotProcessStatus | null,
  /** 最近一次渲染进程上报的视口矩形（C-lite 几何同步缓存；DIP、相对内容区）。 */
  viewportRect: null as ViewportRect | null,
};
