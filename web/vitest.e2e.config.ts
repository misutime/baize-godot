/**
 * 端到端集成测试配置（pnpm test:e2e）——spawn headless 编辑器 + 三包链路断言。
 * 与单元测试分离（各包 vitest 只跑自身 src/test，不包含 e2e）：
 * e2e 需要构建产物（bin/ 编辑器 exe）+ 编辑器进程，耗时 ~10s。
 */
import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["tests/e2e/**/*.test.ts"],
    testTimeout: 60000,
    hookTimeout: 60000,
  },
});
