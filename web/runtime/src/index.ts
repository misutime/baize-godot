/**
 * S0 CLI 入口：测试专用本地 WS server（127.0.0.1，port 0 默认 / --port 或 BAIZE_TEST_PORT 覆盖）。
 *
 * 注意：本 server 无 token/Origin 校验——生产认证路径（双令牌 + main frame 校验）属 S1/S2（§4.3 审查修订）；
 * S0 验收仅要求「测试专用 127.0.0.1:0 WS」上 echo 可配对返回（验收 2）。
 */
import { WebSocketServer } from "ws";

import { JsonRpcDispatcher } from "./jsonrpc";
import { echoHandler } from "./services/echo";

function parsePort(argv: string[]): number {
  const idx = argv.indexOf("--port");
  const value = idx >= 0 ? argv[idx + 1] : process.env.BAIZE_TEST_PORT;
  if (value === undefined) {
    return 0;
  }
  const port = Number(value);
  if (!Number.isInteger(port) || port < 0 || port > 65535) {
    console.error(`[sidecar] 非法端口: ${value}（应为 0-65535 整数）`);
    process.exit(2);
  }
  return port;
}

const port = parsePort(process.argv.slice(2));

const dispatcher = new JsonRpcDispatcher();
dispatcher.register("sidecar.echo", echoHandler);

const wss = new WebSocketServer({ host: "127.0.0.1", port });

wss.on("listening", () => {
  const addr = wss.address();
  if (typeof addr === "object" && addr !== null) {
    console.log(`[sidecar] S0 test WS listening on ws://127.0.0.1:${addr.port}`);
  }
});

wss.on("connection", (ws) => {
  ws.on("message", (data) => {
    const text = data.toString();
    void dispatcher.handleFrame(text).then((reply) => {
      if (reply !== null && ws.readyState === 1 /* OPEN */) {
        ws.send(reply);
      }
    });
  });
  ws.on("error", (err) => {
    console.error("[sidecar] ws connection 错误:", err);
  });
});

wss.on("error", (err) => {
  console.error("[sidecar] WS server 错误:", err);
  process.exit(1);
});

// 优雅退出：S0 仅处理 Ctrl+C；S1 起由 Godot 经 shutdown 通知驱动（§4.4 退出编排）。
for (const signal of ["SIGINT", "SIGTERM"] as const) {
  process.on(signal, () => {
    wss.close(() => process.exit(0));
  });
}
