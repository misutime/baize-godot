import { defineConfig } from "tsdown";

// 主进程：ESM bundle（godot-process/godot-rpc 等 workspace 依赖打进产物，自包含）。
// electron 保持外部（运行时由 Electron 提供）。
export default defineConfig({
  entry: ["electron/main.ts"],
  format: ["esm"],
  target: "node24",
  outDir: "dist-electron",
  clean: false, // 与另一 config 共用 outDir：统一由 build 脚本清理
  sourcemap: true,
  external: ["electron"],
  noExternal: [/@baize\//], // workspace 包打进产物（运行时自包含，Node 无法直接跑 TS 源码）
  fixedExtension: false, // package type: module → ESM 产物用 .js
});
