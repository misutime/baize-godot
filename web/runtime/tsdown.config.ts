import { defineConfig } from "tsdown";

// S0 单入口 ESM 产物（dist/index.js）。S4 发布态按服务边界多产物 + SEA bundle（§3.1 审查修订 P1-9）。
export default defineConfig({
  entry: ["src/index.ts"],
  format: ["esm"],
  target: "node20",
  clean: true,
  sourcemap: true,
  fixedExtension: false, // package type: module，ESM 产物用 .js（验收：node dist/index.js 等价运行）
});
