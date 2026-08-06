import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import { resolve } from "node:path";
import { defineConfig } from "vite";

/**
 * 渲染进程构建（vite 标准）：React + Tailwind 4。
 * 主进程/preload 不经 vite——由 tsc（TS7）编译到 dist-electron/（见 tsconfig.node.json）。
 * dev：vite dev server（渲染进程 HMR）+ electron 加载 localhost（NODE_ENV=development）。
 * prod：vite build → dist/，electron 加载 dist/index.html。
 */
export default defineConfig({
  base: "./", // Electron loadFile（file://）下 assets 相对路径解析
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      "@": resolve(__dirname, "src/renderer/src"),
    },
  },
  build: {
    outDir: "dist",
  },
  server: {
    port: 5173,
    strictPort: true,
  },
});
