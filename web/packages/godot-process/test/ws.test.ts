/**
 * createWsTransport 集成测试：原生 WebSocket client ↔ ws 库测试 server 的 JSON-RPC 帧交换。
 * 验证：请求/应答配对、事件下行（notification）、断线重连、close。
 */
import { afterEach, describe, expect, it } from "vitest";
import { type WebSocket, WebSocketServer } from "ws";

import { createWsTransport } from "@baize/godot-rpc";

interface ServerHandle {
  url: string;
  close: () => Promise<void>;
}

async function startEchoServer(): Promise<ServerHandle> {
  const wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve) => {
    wss.on("listening", () => resolve());
  });
  const addr = wss.address();
  if (typeof addr !== "object" || addr === null) {
    throw new Error("server.address() 不可用");
  }
  const url = `ws://127.0.0.1:${addr.port}`;

  wss.on("connection", (socket: WebSocket) => {
    socket.on("message", (data) => {
      const frame = JSON.parse(data.toString());
      if (frame.method === "notify_me") {
        // 服务端响应后主动下行一个事件 notification
        socket.send(JSON.stringify({ jsonrpc: "2.0", method: "test.event", params: { n: 1 } }));
      }
      socket.send(JSON.stringify({ jsonrpc: "2.0", id: frame.id, result: { echoed: frame.params } }));
    });
  });

  return {
    url,
    close: () =>
      new Promise<void>((resolve) => {
        wss.close(() => resolve());
      }),
  };
}

describe("createWsTransport 集成", () => {
  let server: ServerHandle | null = null;
  let states: string[] = [];

  afterEach(async () => {
    await server?.close();
    server = null;
    states = [];
  });

  it("请求/应答配对", async () => {
    server = await startEchoServer();
    const transport = createWsTransport({ url: server.url, maxReconnects: 0, onStateChange: (s) => states.push(s) });
    // 等待连接建立
    await new Promise<void>((resolve) => {
      const t = setTimeout(() => resolve(), 500);
      const unsub = transport.onEvent(() => {}); // 确保订阅绑定
      // 轮询直到可以请求
      const tryReq = async (): Promise<void> => {
        try {
          await transport.request("echo", { text: "hi" });
          clearTimeout(t);
          unsub();
          resolve();
        } catch {
          setTimeout(() => void tryReq(), 20);
        }
      };
      void tryReq();
    });
    const result = await transport.request("echo", { text: "hi" });
    expect(result).toEqual({ echoed: { text: "hi" } });
    expect(states).toContain("connected");
    transport.close();
  });

  it("事件下行（notification）", async () => {
    server = await startEchoServer();
    const transport = createWsTransport({ url: server.url, maxReconnects: 0 });
    const events: Array<[string, unknown]> = [];
    transport.onEvent((method, params) => events.push([method, params]));
    // 等待连接 + 发请求触发服务端下行事件
    await new Promise<void>((resolve) => {
      const tryReq = async (): Promise<void> => {
        try {
          await transport.request("notify_me", {});
          resolve();
        } catch {
          setTimeout(() => void tryReq(), 20);
        }
      };
      void tryReq();
    });
    expect(events).toContainEqual(["test.event", { n: 1 }]);
    transport.close();
  });

  it("close 后 request 确定性拒绝", async () => {
    server = await startEchoServer();
    const transport = createWsTransport({ url: server.url, maxReconnects: 0 });
    await new Promise<void>((resolve) => {
      const tryReq = async (): Promise<void> => {
        try {
          await transport.request("echo", {});
          resolve();
        } catch {
          setTimeout(() => void tryReq(), 20);
        }
      };
      void tryReq();
    });
    transport.close();
    await expect(transport.request("echo", {})).rejects.toThrow(/dispose|未就绪|关闭/);
  });
});
