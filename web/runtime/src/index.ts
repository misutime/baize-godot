/**
 * S1 CLI 入口：Godot 面连接分派。
 *
 * - 有 BAIZE_GODOT_WS_URL → S1 主路径：GodotClient 连 Godot WS（sidecar.hello 握手 + 认证 deadline 3s + 退避重连），
 *   不再起本地测试 WS server（Godot spawn sidecar 时经 env 下发 URL/token，§4.3/§4.4）。
 * - 无 → S0 兼容形态：测试专用本地 WS server（127.0.0.1，port 0 默认 / --port 或 BAIZE_TEST_PORT 覆盖）。
 *   注意：该形态无 token/Origin 校验——仅 standalone/dev 调试用；生产认证路径（双令牌 + main frame 校验）属 S1/S2（§4.3）。
 */
import { WebSocketServer } from "ws";

import { GodotClient } from "./godot-client";
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

function main(): void {
  const wsUrl = process.env.BAIZE_GODOT_WS_URL;
  if (wsUrl !== undefined && wsUrl !== "") {
    startGodotClient(wsUrl);
    return;
  }
  startTestServer();
}

/** S1 主路径：GodotClient 连 Godot WS；连接状态/握手结果日志由 GodotClient 与 onReady 以 [sidecar] 前缀输出。 */
function startGodotClient(url: string): void {
  let client: GodotClient;
  try {
    client = new GodotClient({
      url,
      token: process.env.BAIZE_GODOT_TOKEN ?? "",
      projectPath: process.env.BAIZE_PROJECT_PATH,
      onReady: (_client, hello) => {
        console.log(`[sidecar] sidecar.hello 应答: ok=${hello.ok} version=${hello.version}`);
      },
    });
  } catch (err) {
    console.error("[sidecar] GodotClient 初始化失败:", err instanceof Error ? err.message : String(err));
    process.exit(1);
  }
  client.connect();
  // 退出编排：S1 起由 Godot 经 ProcessSupervisor/shutdown 通知驱动（§4.4）；此处仅兜底 Ctrl+C。
  for (const signal of ["SIGINT", "SIGTERM"] as const) {
    process.on(signal, () => {
      client.dispose();
      process.exit(0);
    });
  }
}

/** S0 兼容形态：测试专用本地 WS server（无 token 校验，standalone/dev 调试用）。 */
function startTestServer(): void {
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
}

main();
