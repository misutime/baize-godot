/**
 * gd_provider 端到端集成测试（vitest e2e，task verify-provider / pnpm test:e2e）。
 *
 * 前置：需先 `task dev` 构建编辑器（bin/godot.windows.editor.dev.x86_64.console.exe）；
 * 测试项目：test-projects/provider（仓库内）。
 *
 * 用途：C++ Provider 行为验证（Godot 模块无单测框架，端到端断言为可靠方式）；
 * 三包（godot-rpc/godot-process/godot-sdk）↔ Provider 连通回归。
 */
import { spawn, type ChildProcess } from "node:child_process";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import { createClient } from "../../packages/godot-sdk/src/index.ts";
import { GodotClient } from "../../packages/godot-process/src/godot-client.ts";
import { createWsTransport } from "../../packages/godot-rpc/src/index.ts";

const REPO_ROOT = fileURLToPath(new URL("../../../", import.meta.url));
const GODOT_EXE = `${REPO_ROOT}bin/godot.windows.editor.dev.x86_64.console.exe`;
const TEST_PROJECT = `${REPO_ROOT}test-projects/provider`;
const PROVIDER_URL = "ws://127.0.0.1:23009";

let child: ChildProcess | null = null;

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function waitForLog(c: ChildProcess, pattern: string, timeoutMs = 30000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  let buffer = "";
  await new Promise<void>((resolve, reject) => {
    const onData = (chunk: Buffer) => {
      buffer += chunk.toString();
      if (buffer.includes(pattern)) {
        cleanup();
        resolve();
      }
    };
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error(`等待日志超时（${pattern}），最后输出: ${buffer.slice(-300)}`));
    }, timeoutMs);
    const cleanup = () => {
      clearTimeout(timer);
      c.stdout?.off("data", onData);
      c.stderr?.off("data", onData);
    };
    c.stdout?.on("data", onData);
    c.stderr?.on("data", onData);
  });
}

async function connectSdk(): Promise<ReturnType<typeof createClient> & { close: () => void }> {
  const transport = createWsTransport({ url: PROVIDER_URL, maxReconnects: 0 });
  for (let i = 0; i < 50; i++) {
    try {
      await transport.request("hello", { token: "" });
      return Object.assign(createClient(transport), { close: () => transport.close() });
    } catch {
      await sleep(50);
    }
  }
  throw new Error("连接 Provider 失败");
}

describe("gd_provider 端到端（headless 编辑器 + 三包链路）", () => {
  beforeAll(async () => {
    child = spawn(GODOT_EXE, ["--path", TEST_PROJECT, "--editor", "--headless"], {
      stdio: ["ignore", "pipe", "pipe"],
      windowsHide: true,
    });
    await waitForLog(child, "gd_provider] WS server 就绪");
  }, 60000);

  afterAll(() => {
    child?.kill();
    child = null;
  });

  it("godot-rpc createWsTransport：hello 握手 + get_state", async () => {
    const transport = createWsTransport({ url: PROVIDER_URL, maxReconnects: 0 });
    const hello = await transport.request("hello", { token: "" });
    expect(hello.ok).toBe(true);
    expect(typeof hello.version).toBe("string");
    const state = await transport.request("editor.get_state");
    expect(state.has_scene).toBe(true);
    expect(Array.isArray(state.selection)).toBe(true);
    transport.close();
  });

  it("godot-process GodotClient：认证握手 + invoke 读位置", async () => {
    const client = new GodotClient({ url: PROVIDER_URL, token: "", backoffSeconds: [0.05], maxReconnects: 2 });
    const deadline = Date.now() + 5000;
    while (!client.isConnected && Date.now() < deadline) {
      await sleep(30);
    }
    expect(client.epoch).toBe(1);
    const pos = await client.invoke("scene.get_node_position", { node_path: "./Cube" });
    expect(pos).toEqual({ x: 1.5, y: 2, z: -3 });
    client.dispose();
  });

  it("godot-sdk createClient：位置写入 + 回读 + 还原", async () => {
    const sdk = await connectSdk();
    try {
      const before = await sdk.scene.get_node_position({ node_path: "./Cube" });
      await sdk.scene.set_node_position({ node_path: "./Cube", position: { x: 7.25, y: 6.5, z: -5.75 } });
      const after = await sdk.scene.get_node_position({ node_path: "./Cube" });
      expect(after).toEqual({ x: 7.25, y: 6.5, z: -5.75 });
      // 还原场景位置（测试项目保持干净）
      await sdk.scene.set_node_position({ node_path: "./Cube", position: before });
    } finally {
      sdk.close();
    }
  });

  it("错误契约：路径逃逸/不存在节点/未注册方法/溢出数字", async () => {
    const sdk = await connectSdk();
    try {
      const expectErr = async (fn: () => Promise<unknown>, code: string) => {
        try {
          await fn();
          return false;
        } catch (e) {
          const err = e as { data?: { code?: string }; message?: string };
          return err.data?.code === code || err.message?.includes(code);
        }
      };
      expect(await expectErr(() => sdk.scene.get_node_position({ node_path: "../escape" }), "invalid_params")).toBe(true);
      expect(await expectErr(() => sdk.scene.get_node_position({ node_path: "./Nope" }), "invalid_node")).toBe(true);
      expect(await expectErr(() => sdk.transport.request("nope.method", {}), "method_not_found")).toBe(true);
      expect(
        await expectErr(
          () => sdk.scene.set_node_position({ node_path: "./Cube", position: { x: 1e400, y: 0, z: 0 } }),
          "invalid_params",
        ),
      ).toBe(true);
    } finally {
      sdk.close();
    }
  });
});
