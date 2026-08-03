import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// WebDock 页面构建（工作项 3 验收）：base:'./' 使产物可经 file:// 相对加载
// （bin/webview/ui/ 由编辑器 OSR 加载，无 http 服务器）。
export default defineConfig({
  plugins: [react()],
  base: "./",
  build: {
    outDir: "dist",
    target: "es2022",
  },
});
