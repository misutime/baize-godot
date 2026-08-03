import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "node", // 无 DOM：桥对象经 _setBridgeClientForTest 注入（见 transport.test.ts）
  },
});
