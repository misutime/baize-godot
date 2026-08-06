/**
 * 真实 WS 回环（127.0.0.1:0，验收 2 的「测试专用 127.0.0.1:0 WS」形态）：
 * ws 客户端 ↔ 测试专用 server 的 JSON-RPC 帧交换。
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { WebSocket, WebSocketServer } from "ws";

import { JsonRpcDispatcher, RPC_ERROR } from "../src/jsonrpc";
import { echoHandler } from "../src/services/echo";

let server: WebSocketServer;
let url = "";

function makeServer(): JsonRpcDispatcher {
  const d = new JsonRpcDispatcher();
  d.register("sidecar.echo", echoHandler);
  return d;
}

beforeAll(async () => {
  server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  const dispatcher = makeServer();
  await new Promise<void>((resolve) => {
    server.on("listening", () => resolve());
  });
  const addr = server.address();
  if (typeof addr !== "object" || addr === null) {
    throw new Error("server.address() 不可用");
  }
  url = `ws://127.0.0.1:${addr.port}`;
  server.on("connection", (ws) => {
    ws.on("message", (data) => {
      void dispatcher.handleFrame(data.toString()).then((reply) => {
        if (reply !== null && ws.readyState === 1 /* OPEN */) {
          ws.send(reply);
        }
      });
    });
    ws.on("error", () => {
      // 客户端断连：无操作
    });
  });
});

afterAll(
  () =>
    new Promise<void>((resolve) => {
      server.close(() => resolve());
    }),
);

/** 单请求-应答往返。 */
function roundTrip(frame: string): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const timer = setTimeout(() => {
      ws.close();
      reject(new Error("WS 往返超时"));
    }, 2000);
    ws.on("open", () => ws.send(frame));
    ws.on("message", (data) => {
      clearTimeout(timer);
      ws.close();
      resolve(JSON.parse(data.toString()));
    });
    ws.on("error", (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });
}

describe("WS 回环（127.0.0.1:0，测试专用）", () => {
  it("sidecar.echo 配对返回", async () => {
    const res = await roundTrip(
      JSON.stringify({ jsonrpc: "2.0", id: "w1", method: "sidecar.echo", params: { text: "hello ws" } }),
    );
    expect(res).toMatchObject({ jsonrpc: "2.0", id: "w1", result: { text: "hello ws" } });
  });

  it("未知方法 → -32601", async () => {
    const res = await roundTrip(JSON.stringify({ jsonrpc: "2.0", id: "w2", method: "no.such" }));
    expect(res).toMatchObject({ id: "w2", error: { code: RPC_ERROR.METHOD_NOT_FOUND } });
  });

  it("非法 JSON → -32700", async () => {
    const res = await roundTrip("{broken");
    expect(res).toMatchObject({ jsonrpc: "2.0", id: null, error: { code: RPC_ERROR.PARSE_ERROR } });
  });

  it("batch → -32600", async () => {
    const res = await roundTrip(
      JSON.stringify([
        { jsonrpc: "2.0", id: "w3", method: "sidecar.echo" },
        { jsonrpc: "2.0", id: "w4", method: "sidecar.echo" },
      ]),
    );
    expect(res).toMatchObject({ error: { code: RPC_ERROR.INVALID_REQUEST } });
  });
});
