/**
 * 协议向量（§5.1 线级合同，验收 5）：编解码/错误码/notification/batch/string id/超时/配对/迟到应答。
 * 固定 fixture：成功、各错误、UTF-8、长数字 id、重复 id、非法 JSON、缺字段。
 */
import { describe, expect, it } from "vitest";

import { JsonRpcDispatcher, RPC_ERROR, RpcClient, RpcTimeoutError } from "../src/jsonrpc";

/** 构造合法 request 帧。 */
function req(id: string, method: string, params?: unknown): string {
  return JSON.stringify({ jsonrpc: "2.0", id, method, ...(params === undefined ? {} : { params }) });
}

/** 处理一帧并断言有响应，返回解析后的响应对象。 */
async function handle(d: JsonRpcDispatcher, frame: string): Promise<Record<string, unknown>> {
  const out = await d.handleFrame(frame);
  expect(out).not.toBeNull();
  return JSON.parse(out as string) as Record<string, unknown>;
}

describe("JsonRpcDispatcher 协议向量（§5.1）", () => {
  it("合法 request → result 响应（id 回显）", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", (params) => ({ ok: true, result: params }));
    expect(await handle(d, req("rpc_1", "sidecar.echo", { text: "hi" }))).toEqual({
      jsonrpc: "2.0",
      id: "rpc_1",
      result: { text: "hi" },
    });
  });

  it("非法 JSON → -32700（id null）", async () => {
    const d = new JsonRpcDispatcher();
    expect(await handle(d, "{not json")).toMatchObject({
      jsonrpc: "2.0",
      id: null,
      error: { code: RPC_ERROR.PARSE_ERROR },
    });
  });

  it("非对象（字符串/数字/null）→ -32600", async () => {
    const d = new JsonRpcDispatcher();
    for (const raw of ['"str"', "42", "null", "true"]) {
      expect(await handle(d, raw)).toMatchObject({ error: { code: RPC_ERROR.INVALID_REQUEST } });
    }
  });

  it("非 2.0 / 缺 jsonrpc → -32600", async () => {
    const d = new JsonRpcDispatcher();
    for (const raw of ['{"id":"a","method":"x"}', '{"jsonrpc":"1.0","id":"a","method":"x"}']) {
      expect(await handle(d, raw)).toMatchObject({ error: { code: RPC_ERROR.INVALID_REQUEST } });
    }
  });

  it("batch 数组 → -32600（合同显式拒绝）", async () => {
    const d = new JsonRpcDispatcher();
    const res = await handle(d, `[${req("a", "sidecar.echo")}, ${req("b", "sidecar.echo")}]`);
    expect(res).toMatchObject({ jsonrpc: "2.0", id: null, error: { code: RPC_ERROR.INVALID_REQUEST } });
  });

  it("request id 非 string（数字/对象/null）→ -32600", async () => {
    const d = new JsonRpcDispatcher();
    for (const id of ["1", '{"x":1}', "null"]) {
      const res = await handle(d, `{"jsonrpc":"2.0","id":${id},"method":"sidecar.echo"}`);
      expect(res).toMatchObject({ error: { code: RPC_ERROR.INVALID_REQUEST } });
    }
  });

  it("未知方法 → -32601 且 id 回显", async () => {
    const d = new JsonRpcDispatcher();
    expect(await handle(d, req("rpc_9", "no.such.method"))).toMatchObject({
      id: "rpc_9",
      error: { code: RPC_ERROR.METHOD_NOT_FOUND },
    });
  });

  it("handler 业务失败 {ok:false} → -32000 + data.code（内部码不入数值 code）", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", () => ({ ok: false, error: { code: "invalid_params", message: "bad" } }));
    const parsed = await handle(d, req("rpc_2", "sidecar.echo"));
    expect(parsed.error).toMatchObject({ code: RPC_ERROR.BIZ_ERROR, data: { code: "invalid_params" } });
  });

  it("handler 抛异常 → -32603", async () => {
    const d = new JsonRpcDispatcher();
    d.register("boom", () => {
      throw new Error("kaput");
    });
    expect(await handle(d, req("rpc_3", "boom"))).toMatchObject({
      id: "rpc_3",
      error: { code: RPC_ERROR.INTERNAL_ERROR },
    });
  });

  it("handler 返回值非法 → -32603（防御，不静默）", async () => {
    const d = new JsonRpcDispatcher();
    d.register("weird", () => 42 as never);
    expect(await handle(d, req("rpc_4", "weird"))).toMatchObject({
      error: { code: RPC_ERROR.INTERNAL_ERROR },
    });
  });

  it("notification（无 id）→ 执行 handler 且无响应", async () => {
    const d = new JsonRpcDispatcher();
    let calls = 0;
    d.register("sidecar.notify", () => {
      calls += 1;
      return { ok: true, result: null };
    });
    const out = await d.handleFrame('{"jsonrpc":"2.0","method":"sidecar.notify","params":{}}');
    expect(out).toBeNull();
    expect(calls).toBe(1);
  });

  it("server 收到 response 输入 → 拒绝 -32600（合同：Godot 侧不发 request）", async () => {
    const d = new JsonRpcDispatcher();
    expect(await handle(d, '{"jsonrpc":"2.0","id":"rpc_1","result":1}')).toMatchObject({
      id: null,
      error: { code: RPC_ERROR.INVALID_REQUEST },
    });
  });

  it("UTF-8 中文 params 往返", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", (params) => ({ ok: true, result: params }));
    expect(await handle(d, req("rpc_5", "sidecar.echo", { text: "侧边车 🚗 中文" }))).toEqual({
      jsonrpc: "2.0",
      id: "rpc_5",
      result: { text: "侧边车 🚗 中文" },
    });
  });

  it("长数字字符串 id 往返不丢精度（C++ double 陷阱规避）", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", (params) => ({ ok: true, result: params }));
    const longId = "90071992547409931234567890";
    expect((await handle(d, req(longId, "sidecar.echo"))).id).toBe(longId);
  });

  it("重复 id 的两个请求 → 各自独立响应（server 无状态）", async () => {
    const d = new JsonRpcDispatcher();
    d.register("sidecar.echo", (params) => ({ ok: true, result: params }));
    expect(await handle(d, req("dup", "sidecar.echo", { n: 1 }))).toMatchObject({
      id: "dup",
      result: { n: 1 },
    });
    expect(await handle(d, req("dup", "sidecar.echo", { n: 2 }))).toMatchObject({
      id: "dup",
      result: { n: 2 },
    });
  });

  it("错误响应帧缺字段（error 非法）→ -32600", async () => {
    const d = new JsonRpcDispatcher();
    expect(await handle(d, '{"jsonrpc":"2.0","id":"x","error":{"code":"nope"}}')).toMatchObject({
      error: { code: RPC_ERROR.INVALID_REQUEST },
    });
  });
});

describe("RpcClient req_id 配对（与 sdk transport 同构）", () => {
  it("invoke 发送 string id 帧并配对响应 → resolve", async () => {
    const sent: string[] = [];
    const client = new RpcClient((text) => sent.push(text));
    const p = client.invoke("sidecar.echo", { text: "x" });
    expect(sent).toHaveLength(1);
    const sentObj = JSON.parse(sent[0]) as { id: string };
    expect(sentObj.id).toMatch(/^rpc_\d+$/);
    expect(typeof sentObj.id).toBe("string");
    client.handleFrame(JSON.stringify({ jsonrpc: "2.0", id: sentObj.id, result: { text: "x" } }));
    await expect(p).resolves.toEqual({ text: "x" });
    expect(client.pendingCount()).toBe(0);
  });

  it("error 响应 → reject RpcCallError（code/message/data.code）", async () => {
    let lastId = "";
    const client = new RpcClient((text) => {
      lastId = (JSON.parse(text) as { id: string }).id;
    });
    const p = client.invoke("scene.get_node_count");
    client.handleFrame(
      JSON.stringify({
        jsonrpc: "2.0",
        id: lastId,
        error: { code: RPC_ERROR.BIZ_ERROR, message: "引擎错误", data: { code: "engine_error" } },
      }),
    );
    await expect(p).rejects.toMatchObject({
      name: "RpcCallError",
      code: RPC_ERROR.BIZ_ERROR,
      message: "引擎错误",
      data: { code: "engine_error" },
    });
  });

  it("未知 id 迟到应答 → 丢弃且 pending 不变", async () => {
    let lastId = "";
    const client = new RpcClient((text) => {
      lastId = (JSON.parse(text) as { id: string }).id;
    });
    const p = client.invoke("sidecar.echo", { text: "a" });
    client.handleFrame(JSON.stringify({ jsonrpc: "2.0", id: "rpc_never_existed", result: 1 }));
    expect(client.pendingCount()).toBe(1);
    client.handleFrame(JSON.stringify({ jsonrpc: "2.0", id: lastId, result: { text: "a" } }));
    await expect(p).resolves.toEqual({ text: "a" });
    expect(client.pendingCount()).toBe(0);
  });

  it("超时 → reject RpcTimeoutError 且 pending 清理", async () => {
    const client = new RpcClient(() => {});
    const p = client.invoke("sidecar.echo", undefined, 20);
    await expect(p).rejects.toBeInstanceOf(RpcTimeoutError);
    expect(client.pendingCount()).toBe(0);
  });

  it("failAllPending → 全部拒绝（断线语义）", async () => {
    const client = new RpcClient(() => {});
    const p1 = client.invoke("a");
    const p2 = client.invoke("b");
    client.failAllPending("连接断开");
    await expect(p1).rejects.toThrow("连接断开");
    await expect(p2).rejects.toThrow("连接断开");
    expect(client.pendingCount()).toBe(0);
  });

  it("通知下行 → 监听器收到 method/params；退订生效", async () => {
    const client = new RpcClient(() => {});
    const seen: Array<[string, unknown]> = [];
    const off = client.onNotification((method, params) => seen.push([method, params]));
    client.handleFrame('{"jsonrpc":"2.0","method":"editor.selection_changed","params":{"path":"/root"}}');
    expect(seen).toEqual([["editor.selection_changed", { path: "/root" }]]);
    off();
    client.handleFrame('{"jsonrpc":"2.0","method":"editor.selection_changed","params":{}}');
    expect(seen).toHaveLength(1);
  });

  it("dispose → 拒绝 pending、清监听、不再处理帧", async () => {
    const client = new RpcClient(() => {});
    const p = client.invoke("a");
    client.dispose();
    await expect(p).rejects.toThrow("dispose");
    expect(client.pendingCount()).toBe(0);
    expect(() => client.handleFrame('{"jsonrpc":"2.0","method":"x"}')).not.toThrow();
  });

  it("dispose 后 invoke → 立即拒绝", async () => {
    const client = new RpcClient(() => {});
    client.dispose();
    await expect(client.invoke("a")).rejects.toThrow("dispose");
  });
});
