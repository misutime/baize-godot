/** echo 服务语义（in-memory 分派，验收 2 的 in-memory 形态）。 */
import { describe, expect, it } from "vitest";

import { JsonRpcDispatcher, RPC_ERROR } from "../src/jsonrpc";
import { echoHandler } from "../src/services/echo";

/** 处理一帧并断言有响应，返回解析后的响应对象。 */
async function handle(d: JsonRpcDispatcher, frame: string): Promise<Record<string, unknown>> {
  const out = await d.handleFrame(frame);
  expect(out).not.toBeNull();
  return JSON.parse(out as string) as Record<string, unknown>;
}

describe("sidecar.echo（in-memory）", () => {
  it("合法 text → {ok:true, result:{text,ts}}", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", echoHandler);
    const parsed = await handle(
      d,
      JSON.stringify({ jsonrpc: "2.0", id: "e1", method: "sidecar.echo", params: { text: "hello" } }),
    );
    expect(parsed.id).toBe("e1");
    expect((parsed.result as { text: string }).text).toBe("hello");
    expect(typeof (parsed.result as { ts: number }).ts).toBe("number");
  });

  it("缺 text / text 非字符串 → -32000 + data.code=invalid_params", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", echoHandler);
    for (const params of [undefined, {}, { text: 42 }, { text: null }]) {
      const parsed = await handle(
        d,
        JSON.stringify({
          jsonrpc: "2.0",
          id: "e2",
          method: "sidecar.echo",
          ...(params === undefined ? {} : { params }),
        }),
      );
      expect((parsed.error as { code: number }).code).toBe(RPC_ERROR.BIZ_ERROR);
      expect((parsed.error as { data: { code: string } }).data.code).toBe("invalid_params");
    }
  });
});
