import { defineConfig } from "tsdown";

// preload：CommonJS（Electron sandbox 要求 preload 非 ESM）；package type: module 下
// cjs format 输出 .cjs（main.ts 引用 preload.cjs）。
export default defineConfig({
  entry: ["electron/preload.ts"],
  format: ["cjs"],
  target: "node24",
  outDir: "dist-electron",
  clean: false,
  sourcemap: true,
  external: ["electron"],
});
