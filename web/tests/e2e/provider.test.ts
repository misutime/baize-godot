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
import { cpSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import { createClient, type TreeNode } from "../../packages/godot-sdk/src/index.ts";
import { GodotClient } from "../../packages/godot-process/src/godot-client.ts";
import { createWsTransport } from "../../packages/godot-rpc/src/index.ts";

const REPO_ROOT = fileURLToPath(new URL("../../../", import.meta.url));
// 跨平台 exe（review P2）：与 Taskfile 的 GODOT_EXE 规则一致
const GODOT_EXE = `${REPO_ROOT}bin/${
  process.platform === "win32"
    ? "godot.windows.editor.dev.x86_64.console.exe"
    : "godot.macos.editor.dev.arm64"
}`;
const TEST_PROJECT = `${REPO_ROOT}test-projects/provider`;
const PROVIDER_PORT = process.env.BAIZE_PROVIDER_PORT ?? "23009";
const PROVIDER_TOKEN = process.env.BAIZE_PROVIDER_TOKEN ?? "";
const PROVIDER_URL = `ws://127.0.0.1:${PROVIDER_PORT}`;

let child: ChildProcess | null = null;
// 测试项目副本：save_scene_as 会改写场景并更新 scene_file_path（Godot 保存管线规范化：去 load_steps/加 unique_id），
// 直接在 test-projects/provider 上跑会污染仓库——全部测试在临时副本上执行（review P2）。
let projectCopy: string | null = null;

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

/** 递归在场景树中按 path 查找节点（找不到返回 null）。 */
function findNode(tree: TreeNode | null, path: string): TreeNode | null {
  if (!tree) {
    return null;
  }
  if (tree.path === path) {
    return tree;
  }
  for (const child of tree.children) {
    const found = findNode(child, path);
    if (found) {
      return found;
    }
  }
  return null;
}

async function connectSdk(): Promise<ReturnType<typeof createClient> & { close: () => void }> {
  const transport = createWsTransport({ url: PROVIDER_URL, maxReconnects: 0 });
  for (let i = 0; i < 50; i++) {
    try {
      await transport.request("hello", { token: PROVIDER_TOKEN });
      return Object.assign(createClient(transport), { close: () => transport.close() });
    } catch {
      await sleep(50);
    }
  }
  throw new Error("连接 Provider 失败");
}

describe("gd_provider 端到端（headless 编辑器 + 三包链路）", () => {
  beforeAll(async () => {
    // 临时副本（排除 .godot 编辑器缓存；首次启动 Godot 自动导入）
    projectCopy = mkdtempSync(join(tmpdir(), "baize-e2e-"));
    cpSync(TEST_PROJECT, projectCopy, {
      recursive: true,
      filter: (src) => !src.includes(join(TEST_PROJECT, ".godot")),
    });
    child = spawn(GODOT_EXE, ["--path", projectCopy, "--editor", "--headless", "res://main.tscn"], {
      stdio: ["ignore", "pipe", "pipe"],
      windowsHide: true,
    });
    await waitForLog(child, "gd_provider] WS server 就绪");
  }, 60000);

  afterAll(async () => {
    // 等 Godot 完全退出（Windows 文件句柄释放）再删副本，避免 EPERM；
    // kill 在 Windows 为强制终止，exit 事件必然触发（异常挂起由 vitest hookTimeout 兜底）。
    if (child && child.exitCode === null && !child.killed) {
      const exited = new Promise<void>((resolve) => {
        child!.once("exit", () => resolve());
      });
      child.kill();
      await exited;
    } else {
      child?.kill();
    }
    child = null;
    if (projectCopy) {
      // 包装器 kill 后 GUI 子进程经 Job 异步清理，句柄释放滞后于 exit 事件——
      // 重试删除（最多 5s）；这是等待真实 OS 句柄释放，非猜测时长。
      let lastErr: unknown;
      for (let i = 0; i < 25; i++) {
        try {
          rmSync(projectCopy, { recursive: true, force: true });
          lastErr = undefined;
          break;
        } catch (err) {
          lastErr = err;
          await sleep(200);
        }
      }
      projectCopy = null;
      if (lastErr) {
        throw lastErr;
      }
    }
  });

  it("godot-rpc createWsTransport：hello 握手 + get_state", async () => {
    const transport = createWsTransport({ url: PROVIDER_URL, maxReconnects: 0 });
    const hello = await transport.request("hello", { token: PROVIDER_TOKEN });
    expect(hello.ok).toBe(true);
    expect(typeof hello.version).toBe("string");
    const state = await transport.request("editor.get_state");
    expect(state.has_scene).toBe(true);
    expect(Array.isArray(state.selection)).toBe(true);
    transport.close();
  });

  it("godot-process GodotClient：认证握手 + invoke 读位置", async () => {
    const client = new GodotClient({ url: PROVIDER_URL, token: PROVIDER_TOKEN, backoffSeconds: [0.05], maxReconnects: 2 });
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

  it("选中/undo/redo：select_node + set_node_position 后 undo 回退、redo 恢复", async () => {
    const sdk = await connectSdk();
    try {
      const before = await sdk.scene.get_node_position({ node_path: "./Cube" });
      await sdk.editor.select_node({ node_path: "./Cube" });
      const state = await sdk.editor.get_state();
      expect(state.selection).toContain("Cube");

      await sdk.scene.set_node_position({ node_path: "./Cube", position: { x: 11, y: 12, z: 13 } });
      expect(await sdk.scene.get_node_position({ node_path: "./Cube" })).toEqual({ x: 11, y: 12, z: 13 });

      await sdk.editor.undo();
      expect(await sdk.scene.get_node_position({ node_path: "./Cube" })).toEqual(before); // undo 回退

      await sdk.editor.redo();
      expect(await sdk.scene.get_node_position({ node_path: "./Cube" })).toEqual({ x: 11, y: 12, z: 13 }); // redo 恢复

      // 还原场景位置
      await sdk.editor.undo();
      await sdk.scene.set_node_position({ node_path: "./Cube", position: before });
      await sdk.editor.undo(); // 撤销还原操作
    } finally {
      sdk.close();
    }
  });

  it("错误契约：路径逃逸/不存在节点/未注册方法/溢出数字", async () => {
    const sdk = await connectSdk();
    try {
      // 同时断言 JSON-RPC 数值码（-32601/-32602/-32000）与内部字符串码（review P3 测试缺口）
      const expectErr = async (
        fn: () => Promise<unknown>,
        code: number,
        internalCode: string,
      ): Promise<boolean> => {
        try {
          await fn();
          return false;
        } catch (e) {
          const err = e as { code?: number; data?: { code?: string }; message?: string };
          return err.code === code && (err.data?.code === internalCode || err.message?.includes(internalCode));
        }
      };
      expect(
        await expectErr(() => sdk.scene.get_node_position({ node_path: "../escape" }), -32602, "invalid_params"),
      ).toBe(true);
      expect(
        await expectErr(() => sdk.scene.get_node_position({ node_path: "./Nope" }), -32000, "invalid_node"),
      ).toBe(true);
      expect(
        await expectErr(() => sdk.transport.request("nope.method", {}), -32601, "method_not_found"),
      ).toBe(true);
      expect(
        await expectErr(
          () => sdk.scene.set_node_position({ node_path: "./Cube", position: { x: 1e400, y: 0, z: 0 } }),
          -32602,
          "invalid_params",
        ),
      ).toBe(true);
      // 有限 double 但单精度溢出（1e308 → float Inf）：必须拒绝（review P1 测试缺口）
      expect(
        await expectErr(
          () => sdk.scene.set_node_position({ node_path: "./Cube", position: { x: 1e308, y: 0, z: 0 } }),
          -32602,
          "invalid_params",
        ),
      ).toBe(true);
    } finally {
      sdk.close();
    }
  });

  it("scene.get_tree：根 Main/Node3D + Cube/Camera3D 子节点", async () => {
    const sdk = await connectSdk();
    try {
      const tree = await sdk.scene.get_tree();
      expect(tree).not.toBeNull();
      // 根：path 固定 "."，name Main，type Node3D
      expect(tree!.path).toBe(".");
      expect(tree!.name).toBe("Main");
      expect(tree!.type).toBe("Node3D");
      // Cube：path 为根 get_path_to 结果（无 "./" 前缀）
      const cube = findNode(tree, "Cube");
      expect(cube).not.toBeNull();
      expect(cube!.path).toBe("Cube");
      expect(cube!.name).toBe("Cube");
      expect(cube!.type).toBe("Node3D");
      // Camera3D
      const cam = findNode(tree, "Camera3D");
      expect(cam).not.toBeNull();
      expect(cam!.type).toBe("Camera3D");
      // Cube 必须是 Main 的直接子节点（而非根自身）
      expect(tree!.children.map((c) => c.name)).toContain("Cube");
      expect(tree!.children.map((c) => c.name)).toContain("Camera3D");
    } finally {
      sdk.close();
    }
  });

  it("editor.save_scene_as：创建 SaveProbe → 保存临时文件含节点 → 移除 → 再保存不含（不污染 main.tscn）", async () => {
    const sdk = await connectSdk();
    // 保存断言走 save_scene_as 系统临时文件：Godot 保存管线会对 main.tscn 做规范化
    // （去 load_steps、自动分配节点 unique_id），无法通过删除节点还原——项目文件不动。
    const tmpScene = join(tmpdir(), `baize-save-probe-${Date.now()}.tscn`);
    try {
      const probeName = `SaveProbe_${Date.now()}`;
      const { node_path } = await sdk.scene.create_node({ type: "Node3D", name: probeName });
      const finalName = node_path.split("/").pop()!; // validate_child_name 后的实际名
      expect(finalName).toBe(probeName);

      const { path } = await sdk.editor.save_scene_as({ path: tmpScene });
      expect(path).toBe(tmpScene);
      const saved = readFileSync(tmpScene, "utf8");
      expect(saved).toContain(`[node name="${finalName}"`);

      // 移除后再次保存 → 文件不再包含该节点（幂等还原）
      await sdk.scene.remove_node({ node_path });
      const { path: path2 } = await sdk.editor.save_scene_as({ path: tmpScene });
      expect(path2).toBe(tmpScene);
      const saved2 = readFileSync(tmpScene, "utf8");
      expect(saved2).not.toContain(`[node name="${finalName}"`);
    } finally {
      rmSync(tmpScene, { force: true });
      sdk.close();
    }
  });

  it("scene.create_node/remove_node + undo/redo 回退", async () => {
    const sdk = await connectSdk();
    try {
      const probeName = `M1Probe_${Date.now()}`;
      // create → 树中出现
      const created = await sdk.scene.create_node({ type: "Node3D", name: probeName });
      expect(created.node_path).toBe(probeName);
      expect(findNode(await sdk.scene.get_tree(), created.node_path)).not.toBeNull();
      // remove → 树中消失
      await sdk.scene.remove_node({ node_path: created.node_path });
      expect(findNode(await sdk.scene.get_tree(), created.node_path)).toBeNull();
      // 再 create（同名）→ 树中出现；undo 撤销本次创建 → 恢复移除后状态；redo → 节点再现
      const created2 = await sdk.scene.create_node({ type: "Node3D", name: probeName });
      expect(findNode(await sdk.scene.get_tree(), created2.node_path)).not.toBeNull();
      await sdk.editor.undo();
      expect(findNode(await sdk.scene.get_tree(), created2.node_path)).toBeNull();
      await sdk.editor.redo();
      expect(findNode(await sdk.scene.get_tree(), created2.node_path)).not.toBeNull();
      // 还原：移除探测节点，保持场景干净
      await sdk.scene.remove_node({ node_path: created2.node_path });
      expect(findNode(await sdk.scene.get_tree(), created2.node_path)).toBeNull();
    } finally {
      sdk.close();
    }
  });

  it("scene.get_props/set_prop：position 属性读改写 + 还原", async () => {
    const sdk = await connectSdk();
    try {
      const props = await sdk.scene.get_props({ node_path: "./Cube" });
      const pos = props.find((p) => p.name === "position");
      expect(pos).toBeDefined();
      expect(pos!.type).toBe("Vector3");
      expect(pos!.editable).toBe(true);

      const before = await sdk.scene.get_node_position({ node_path: "./Cube" });
      const next = { x: before.x + 1, y: before.y - 1, z: before.z + 2 };
      await sdk.scene.set_prop({ node_path: "./Cube", prop: "position", value: next });
      expect(await sdk.scene.get_node_position({ node_path: "./Cube" })).toEqual(next);
      // 还原原值
      await sdk.scene.set_prop({ node_path: "./Cube", prop: "position", value: before });
      expect(await sdk.scene.get_node_position({ node_path: "./Cube" })).toEqual(before);
    } finally {
      sdk.close();
    }
  });

  it("M1 错误契约：未知类创建/未知属性/非法值/根节点删除", async () => {
    const sdk = await connectSdk();
    try {
      const expectErr = async (
        fn: () => Promise<unknown>,
        code: number,
        internalCode: string,
      ): Promise<boolean> => {
        try {
          await fn();
          return false;
        } catch (e) {
          const err = e as { code?: number; data?: { code?: string }; message?: string };
          return err.code === code && (err.data?.code === internalCode || err.message?.includes(internalCode));
        }
      };
      // create_node 不存在的类 → invalid_params
      expect(
        await expectErr(() => sdk.scene.create_node({ type: "NoSuchClassXYZ" }), -32602, "invalid_params"),
      ).toBe(true);
      // set_prop 不存在的属性 → invalid_params
      expect(
        await expectErr(() => sdk.scene.set_prop({ node_path: "./Cube", prop: "no_such_prop", value: 1 }), -32602, "invalid_params"),
      ).toBe(true);
      // set_prop 值结构非法（编码表外类型）→ invalid_params
      expect(
        await expectErr(() => sdk.scene.set_prop({ node_path: "./Cube", prop: "position", value: "abc" }), -32602, "invalid_params"),
      ).toBe(true);
      // remove_node 根节点 "." → invalid_params（UI 禁用根删除，Provider 兜底拒绝）
      expect(await expectErr(() => sdk.scene.remove_node({ node_path: "." }), -32602, "invalid_params")).toBe(true);
    } finally {
      sdk.close();
    }
  });

  it("editor.get_theme/get_scale/get_project_info：信息面返回结构断言", async () => {
    const sdk = await connectSdk();
    try {
      const theme = await sdk.editor.get_theme();
      expect(typeof theme.theme_name).toBe("string");
      expect(typeof theme.preset).toBe("string");
      for (const c of [theme.base_color, theme.accent_color]) {
        expect(typeof c.r).toBe("number");
        expect(typeof c.g).toBe("number");
        expect(typeof c.b).toBe("number");
        expect(typeof c.a).toBe("number");
      }
      expect(theme.font_size).toBeGreaterThan(0);

      const { scale } = await sdk.editor.get_scale();
      expect(scale).toBeGreaterThan(0);

      const info = await sdk.editor.get_project_info();
      expect(info.project_name).toBe("test-project");
      expect(info.main_scene).toBe("res://main.tscn");
      expect(info.rendering_method).toBe("gl_compatibility");
      expect(info.godot_version).toMatch(/^4\.8/);
      // 测试在临时副本上运行（baize-e2e-*），断言路径指向副本而非仓库内项目
      expect(info.project_path).toContain("baize-e2e-");
    } finally {
      sdk.close();
    }
  });
});
