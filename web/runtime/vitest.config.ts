import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "node", // sidecar 纯 Node 服务，无 DOM
  },
});
