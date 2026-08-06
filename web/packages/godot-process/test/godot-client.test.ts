/**
 * GodotClient 集成测试：mock Godot（ws WebSocketServer @ 127.0.0.1:0）模拟 Godot 侧，协议形状与 C++
 * SidecarServer 一致（Godot Provider:486-491：handler {ok,result} → 裸 result 下行）：
 * - hello → 校验 token（正确/错误两分支）→ 应答 result: { ok:true, version }（hello 载荷自带 ok/version）
 *   或拒绝 error: { code:-32000, message, data:{ code:"unauthorized" } }（_jsonrpc_error 统一映射）后断开；
 * - echo → result: 原样 params；scene.get_node_count → result: 裸数字；
 * - hang → 不应答（模拟服务端挂起，用于 pending 断言）；hangHello=true 时 hello 同样不应答（重试耗尽用）。
 *
 * 用例：握手成功（hello 应答 + invoke 可调）、错误 token 明确拒绝、断线退避重连
 * （pending 确定性拒绝 + 新连接 invoke 可用 + epoch 递增）、dispose 停止重连且拒绝 pending、
 * shutdown 通知停止连接循环、未就绪 invoke 确定性拒绝、重试耗尽 pending 全部拒绝。
 * 退避序列经 GodotClientOptions 注入短档（真实短延时 + 轮询等待，确定性）；端口不固定（127.0.0.1:0）。
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { type WebSocket, WebSocketServer } from "ws";

import { GodotClient, type GodotClientOptions } from "../src/godot-client";

const VALID_TOKEN = "test-token-abc123";
const WRONG_TOKEN = "wrong-token-xyz";
const MOCK_VERSION = "mock-godot-1.0";

interface MockGodotHandle {
  url: string;
  helloCalls: number;
  echoCalls: number;
  receivedTokens: string[];
  connections: WebSocket[];
  /** 置 true 后 hello 不应答（模拟服务端挂起；连接保持打开，由测试主动断开）。 */
  hangHello: boolean;
  close: () => Promise<void>;
  /** 服务端主动断开最近一条连接（模拟 Godot 崩溃/重启）。 */
  closeLatestConnection: () => boolean;
}

async function startMockGodot(validToken: string): Promise<MockGodotHandle> {
  const wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  const { promise: listening, resolve: resolveListening } = Promise.withResolvers<void>();
  wss.on("listening", () => resolveListening());
  await listening;
  const addr = wss.address();
  if (typeof addr !== "object" || addr === null) {
    throw new Error("server.address() 不可用");
  }
  const state = {
    helloCalls: 0,
    echoCalls: 0,
    receivedTokens: [] as string[],
    connections: [] as WebSocket[],
    hangHello: false,
  };
  wss.on("connection", (ws) => {
    state.connections.push(ws);
    ws.on("message", (data) => {
      const frame = JSON.parse(data.toString()) as { id?: unknown; method?: unknown; params?: unknown };
      if (frame.method === "hello") {
        state.helloCalls += 1;
        if (state.hangHello) {
          return; // 挂起：不应答（重试耗尽/pending 断言用），连接保持打开
        }
        const token = (frame.params as { token?: unknown } | null | undefined)?.token;
        state.receivedTokens.push(typeof token === "string" ? token : "");
        if (token === validToken) {
          // C++ 形状（Godot Provider:558-564）：hello 载荷 result 自带 ok/version
          ws.send(
            JSON.stringify({ jsonrpc: "2.0", id: frame.id, result: { ok: true, version: MOCK_VERSION } }),
          );
        } else {
          // C++ 形状（Godot Provider:460-469）：-32000 + data.code=unauthorized，随后断开（peer dead）
          ws.send(
            JSON.stringify({
              jsonrpc: "2.0",
              id: frame.id,
              error: { code: -32000, message: "token 校验失败", data: { code: "unauthorized" } },
            }),
          );
          ws.close();
        }
        return;
      }
      if (frame.method === "hang") {
        return; // 挂起：不应答（pending 断言用）
      }
      if (typeof frame.id === "string") {
        // C++ 形状（Godot Provider:486-491）：handler {ok,result} → 裸 result 下行
        if (frame.method === "scene.get_node_count") {
          ws.send(JSON.stringify({ jsonrpc: "2.0", id: frame.id, result: 3 }));
          return;
        }
        if (frame.method === "echo") {
          state.echoCalls += 1;
        }
        ws.send(JSON.stringify({ jsonrpc: "2.0", id: frame.id, result: frame.params ?? null }));
      }
    });
    ws.on("error", () => {
      // 客户端断连：无操作
    });
  });
  return {
    url: `ws://127.0.0.1:${addr.port}`,
    // 计数器用 getter 暴露：直接 spread 会按值拷贝数字，冻结在初始值
    get helloCalls() {
      return state.helloCalls;
    },
    get echoCalls() {
      return state.echoCalls;
    },
    receivedTokens: state.receivedTokens,
    connections: state.connections,
    get hangHello() {
      return state.hangHello;
    },
    set hangHello(value: boolean) {
      state.hangHello = value;
    },
    close: () => {
      const { promise, resolve } = Promise.withResolvers<void>();
      for (const ws of state.connections) {
        ws.terminate();
      }
      wss.close(() => resolve());
      return promise;
    },
    closeLatestConnection: () => {
      const latest = state.connections[state.connections.length - 1];
      if (latest === undefined) {
        return false;
      }
      latest.close();
      return true;
    },
  };
}

function sleep(ms: number): Promise<void> {
  const { promise, resolve } = Promise.withResolvers<void>();
  setTimeout(resolve, ms);
  return promise;
}

/**
 * 轮询等待条件成立。
 *
 * 本测试套件刻意使用真实短延时（而非 fake timers）：被测对象是 ws 真实 socket 连接的
 * 退避/重连行为，ws 库内部依赖真实时钟驱动握手/关闭超时，fake timers 会破坏其时序；
 * 退避序列已注入 10-30ms 短档，等待窗口 ≤3s，确定性由「条件轮询 + 超时抛错」保证（集成测试例外）。
 */
async function waitUntil(predicate: () => boolean, timeoutMs = 3000, intervalMs = 10): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!predicate()) {
    if (Date.now() > deadline) {
      throw new Error("waitUntil 超时");
    }
    await sleep(intervalMs);
  }
}

describe("GodotClient（mock Godot WS @ 127.0.0.1:0）", () => {
  let mock: MockGodotHandle;
  const clients: GodotClient[] = [];

  beforeEach(async () => {
    mock = await startMockGodot(VALID_TOKEN);
  });

  afterEach(async () => {
    vi.restoreAllMocks();
    for (const client of clients) {
      client.dispose();
    }
    clients.length = 0;
    await mock.close();
  });

  /** 构造并登记一个 GodotClient（afterEach 统一 dispose）。 */
  function makeClient(
    options: Omit<GodotClientOptions, "url" | "token"> = {},
    token = VALID_TOKEN,
  ): GodotClient {
    const client = new GodotClient({ url: mock.url, token, ...options });
    clients.push(client);
    return client;
  }

  it("握手成功：hello 应答（ok/version）+ invoke 可调 + epoch=1", async () => {
    const ready = vi.fn();
    const client = makeClient({ backoffSeconds: [0.02], onReady: ready });
    client.connect();
    await waitUntil(() => client.isConnected && ready.mock.calls.length === 1);

    expect(client.epoch).toBe(1);
    expect(client.state).toBe("connected");
    expect(mock.helloCalls).toBe(1);
    expect(ready).toHaveBeenCalledWith(client, { ok: true, version: MOCK_VERSION });

    // C++ 形状：echo handler {ok,result} → 裸 result（原样回 params），非 {ok:true,result} 包装
    const echo = await client.invoke("echo", { text: "hello" }, 2000);
    expect(echo).toEqual({ text: "hello" });
    expect(mock.echoCalls).toBe(1);
  });

  it("错误 token → 握手被拒：明确日志（含 data.code、无 token 明文）+ 重连达上限停止", async () => {
    const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    const client = makeClient({ backoffSeconds: [0.01], maxReconnects: 1 }, WRONG_TOKEN);
    client.connect();
    await waitUntil(() => client.state === "failed");

    expect(mock.helloCalls).toBe(2); // 首次连接 + 1 次重连，均被拒
    expect(mock.receivedTokens).toEqual([WRONG_TOKEN, WRONG_TOKEN]);
    const logs = errorSpy.mock.calls.map((args) => args.join(" ")).join("\n");
    expect(logs).toContain("握手失败");
    expect(logs).toContain("unauthorized"); // data.code 与 C++ _dispatch 内部码一致（§5.1）
    expect(logs).not.toContain(WRONG_TOKEN); // token 明文不落日志
    await expect(client.invoke("scene.get_node_count")).rejects.toThrow("重连达上限");
  });

  it("断线重连：pending 确定性拒绝 + 新连接 invoke 可用 + epoch 递增", async () => {
    const epochs: number[] = [];
    const client = makeClient({
      backoffSeconds: [0.03],
      maxReconnects: 3,
      onReady: (c) => epochs.push(c.epoch),
    });
    client.connect();
    await waitUntil(() => client.isConnected && epochs.length === 1);
    expect(client.epoch).toBe(1);
    expect(mock.connections).toHaveLength(1);

    // 挂起调用（mock 不应答）→ 服务端断开 → failAllPending 确定性拒绝
    const hanging = client.invoke("hang", { n: 1 }, 5000);
    expect(mock.closeLatestConnection()).toBe(true);
    await expect(hanging).rejects.toThrow("连接关闭");

    // 退避重连成功：epoch+1，onReady 再次回调
    await waitUntil(() => client.isConnected && client.epoch === 2);
    expect(epochs).toEqual([1, 2]);
    expect(mock.connections).toHaveLength(2);
    expect(mock.helloCalls).toBe(2);

    // 新连接上 invoke 可用（C++ 形状：get_node_count → 裸数字 result）
    const count = await client.invoke("scene.get_node_count", {}, 2000);
    expect(count).toBe(3);
  });

  it("dispose：停止重连 + 拒绝 pending 与后续 invoke + 不再触发 onReady", async () => {
    const ready = vi.fn();
    const client = makeClient({ backoffSeconds: [0.02], maxReconnects: 5, onReady: ready });
    client.connect();
    await waitUntil(() => client.isConnected && ready.mock.calls.length === 1);
    expect(client.epoch).toBe(1);

    const hanging = client.invoke("hang", undefined, 5000);
    client.dispose();
    await expect(hanging).rejects.toThrow("dispose");
    await expect(client.invoke("echo", { text: "x" })).rejects.toThrow("dispose");
    expect(ready.mock.calls.length).toBe(1); // dispose 后不重连、不再握手

    const connectionsAtDispose = mock.connections.length;
    // 负向断言（“不再重连”）只能对照真实时钟：等 150ms ≫ 退避档 20ms，若 dispose 失效此处必然出现新连接
    await sleep(150);
    expect(mock.connections.length).toBe(connectionsAtDispose);
    expect(mock.helloCalls).toBe(1);
  });

  it("shutdown 通知：客户端 dispose、停止重连（不再有新连接/握手）", async () => {
    const ready = vi.fn();
    const client = makeClient({ backoffSeconds: [0.02], maxReconnects: 5, onReady: ready });
    client.connect();
    await waitUntil(() => client.isConnected && ready.mock.calls.length === 1);
    expect(client.epoch).toBe(1);

    // 服务端 stop() 下行 shutdown 通知（C++ 形状：Godot Provider:130，无 id 的 notification）
    const conn = mock.connections[0];
    conn.send(JSON.stringify({ jsonrpc: "2.0", method: "shutdown" }));

    await waitUntil(() => client.state === "disposed");
    expect(client.isConnected).toBe(false);
    expect(ready.mock.calls.length).toBe(1); // dispose 后不再握手、不触发 onReady

    const connectionsAtShutdown = mock.connections.length;
    // 负向断言（“不再重连”）：等 150ms ≫ 退避档 20ms，若 shutdown 未阻止重连，此处必然出现新连接
    await sleep(150);
    expect(mock.connections.length).toBe(connectionsAtShutdown);
    expect(mock.helloCalls).toBe(1);

    client.dispose(); // dispose 幂等：重复调用安全
    await expect(client.invoke("echo", { text: "x" })).rejects.toThrow("dispose");
  });

  it("未就绪 invoke：connecting 态立即拒绝（不登记 pending、不发帧）", async () => {
    const client = makeClient({ backoffSeconds: [0.02], maxReconnects: 3 });
    client.connect();
    // connect() 同步进入 connecting（WS 连接/握手未完成）
    expect(client.state).toBe("connecting");

    const start = Date.now();
    await expect(client.invoke("echo", { text: "x" }, 5000)).rejects.toThrow(/未就绪|未认证/);
    expect(Date.now() - start).toBeLessThan(500); // 确定性拒绝，不等超时
    expect(mock.echoCalls).toBe(0); // 未登记 pending、未发帧

    // 连接建立后 invoke 恢复正常
    await waitUntil(() => client.isConnected);
    const echo = await client.invoke("echo", { text: "after" }, 2000);
    expect(echo).toEqual({ text: "after" });
    expect(mock.echoCalls).toBe(1);
  });

  it("重试耗尽：pending 全部确定性拒绝（failAllPending 生效），不悬挂到超时", async () => {
    // hello 挂起不应答（60s 超时远未到）+ 服务端主动断开：每次尝试的 hello pending
    // 都由 failAllPending 确定性拒绝（连接断开/重连达上限），而不是悬挂至自身超时。
    mock.hangHello = true;
    const client = makeClient(
      { backoffSeconds: [0.01], maxReconnects: 1, helloTimeoutMs: 60000 },
      VALID_TOKEN,
    );
    client.connect();
    // 等连接建立且 hello 已发出（helloCalls 到位即客户端侧 pending 已登记），再断开
    await waitUntil(() => mock.connections.length === 1 && mock.helloCalls === 1);
    expect(mock.closeLatestConnection()).toBe(true);
    // 第 1 次重连同样挂起 → 断开 → 达上限 failed
    await waitUntil(() => mock.connections.length === 2 && mock.helloCalls === 2);
    expect(mock.closeLatestConnection()).toBe(true);
    await waitUntil(() => client.state === "failed");

    expect(mock.helloCalls).toBe(2);
    expect(client.epoch).toBe(0); // 从未握手成功
    // 耗尽后 invoke 立即确定性拒绝（不会登记 pending 等超时）
    const start = Date.now();
    await expect(client.invoke("scene.get_node_count", {}, 5000)).rejects.toThrow("重连达上限");
    expect(Date.now() - start).toBeLessThan(500);
  });
});
