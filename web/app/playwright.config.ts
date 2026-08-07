import { defineConfig } from "@playwright/test";

/**
 * Electron 应用 e2e（借鉴 electron-vite-react 模板）：
 * 对构建产物跑（pretest: vite build --mode=test）——真实窗口、无 dev server。
 * _electron.launch 使用 node_modules 内的 electron 二进制，无需下载 playwright 浏览器。
 */
export default defineConfig({
  testDir: "./e2e",
  fullyParallel: false,
  workers: 1, // Electron 实例串行：避免多开单实例锁互相干扰
  timeout: 30_000,
  reporter: "list",
  use: {
    trace: "on-first-retry",
  },
});
