import { rmSync } from "node:fs";
import { resolve } from "node:path";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import { electronSimple } from "vite-plugin-electron/multi-env";

/**
 * 单配置构建链（借鉴 electron-vite-react 模板）：
 * - 渲染进程：vite 标准（React + Tailwind 4）；
 * - 主进程/preload：vite-plugin-electron（electronSimple）同配置构建到 dist-electron/，
 *   dev 自动拉起 electron 并注入 VITE_DEV_SERVER_URL，main/preload 改动自动重启；
 * - @baize/* workspace 包是 TS 源码 exports，必须打进主进程产物（不外部化），
 *   仅 electron 保持外部（运行时由 Electron 提供）；node: 内置模块自动外部。
 */
export default defineConfig(({ command }) => {
  rmSync("dist-electron", { recursive: true, force: true });

  const isServe = command === "serve";
  const isBuild = command === "build";
  const sourcemap = isServe || !!process.env.VSCODE_DEBUG;

  return {
    base: "./", // Electron loadFile（file://）下 assets 相对路径解析
    plugins: [
      react(),
      tailwindcss(),
      electronSimple({
        main: {
          input: "electron/main/index.ts",
          options: {
            build: {
              sourcemap,
              minify: isBuild,
              outDir: "dist-electron/main",
              rolldownOptions: {
                external: ["electron"],
              },
            },
          },
        },
        preload: {
          input: "electron/preload/index.ts",
          options: {
            build: {
              sourcemap: sourcemap ? "inline" : undefined,
              minify: isBuild,
              outDir: "dist-electron/preload",
              rolldownOptions: {
                external: ["electron"],
                output: {
                  format: "cjs", // sandbox preload 必须是 CommonJS（electron/main/index.ts 引用 index.cjs）
                  entryFileNames: "index.cjs",
                },
              },
            },
          },
        },
      }),
    ],
    resolve: {
      alias: {
        "@": resolve(import.meta.dirname, "src/renderer/src"),
      },
    },
    build: {
      outDir: "dist",
    },
    server: {
      // 固定 IPv4 loopback：避免 localhost 解析为 ::1（IPv6）与 dev 链路不一致
      host: "127.0.0.1",
      port: 5173,
      strictPort: true,
    },
    clearScreen: false,
  };
});
