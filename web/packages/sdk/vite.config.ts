import { defineConfig } from "vite";

// SDK 库构建（Vite 8 / Rolldown）：双入口（主包 + ./react 子路径），ESM 产物。
// d.ts 由 tsc --emitDeclarationOnly 生成（vite build 不产出声明）。
export default defineConfig({
  build: {
    lib: {
      entry: {
        index: "src/index.ts",
        react: "src/react.ts",
      },
      formats: ["es"],
      fileName: (_format, entryName) => `${entryName}.js`,
    },
    outDir: "dist",
    sourcemap: true,
    rollupOptions: {
      external: ["react"], // peerDependency：hooks 子路径消费方提供
    },
  },
});
