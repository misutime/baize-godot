/**
 * 编解码协议向量（线级合同）：合法帧/各错误路径/batch 拒绝/string id 校验/UTF-8/响应歧义。
 */
import { describe, expect, it } from "vitest";

import { RPC_ERROR, decodeFrame, encodeError, encodeSuccess } from "./codec";

describe("decodeFrame 协议向量（线级合同）", () => {
  it("合法 request → request（id 回显）", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":"rpc_1","method":"hello","params":{"token":"t"}}');
    expect(r.kind).toBe("request");
    if (r.kind === "request") {
      expect(r.request).toEqual({ jsonrpc: "2.0", id: "rpc_1", method: "hello", params: { token: "t" } });
    }
  });

  it("无 params 的 request 省略字段", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":"r1","method":"health"}');
    expect(r.kind).toBe("request");
    if (r.kind === "request") {
      expect(Object.hasOwn(r.request, "params")).toBe(false);
    }
  });

  it("notification（无 id）", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","method":"editor.selection_changed","params":{"node_paths":[]}}');
    expect(r.kind).toBe("notification");
  });

  it("合法 response（result）", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":"r1","result":{"ok":true}}');
    expect(r.kind).toBe("response");
    if (r.kind === "response") {
      expect("result" in r.response).toBe(true);
    }
  });

  it("合法 response（error）", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":"r1","error":{"code":-32000,"message":"业务失败","data":{"code":"no_scene"}}}');
    expect(r.kind).toBe("response");
    if (r.kind === "response" && "error" in r.response) {
      expect(r.response.error.data).toEqual({ code: "no_scene" });
    }
  });

  it("非法 JSON → -32700（id null）", () => {
    const r = decodeFrame("{not json");
    expect(r).toEqual({ kind: "error", error: { code: RPC_ERROR.PARSE_ERROR, message: "Parse error" }, id: null });
  });

  it("batch 数组 → -32600 显式拒绝", () => {
    const r = decodeFrame("[1,2,3]");
    expect(r.kind).toBe("error");
    if (r.kind === "error") {
      expect(r.error.code).toBe(RPC_ERROR.INVALID_REQUEST);
    }
  });

  it("jsonrpc 非 2.0 → -32600", () => {
    const r = decodeFrame('{"jsonrpc":"1.0","id":"r1","method":"x"}');
    expect(r.kind).toBe("error");
  });

  it("request id 非 string → -32600", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":123,"method":"x"}');
    expect(r.kind).toBe("error");
  });

  it("response 同时含 result 与 error → -32600", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":"r1","result":1,"error":{"code":1,"message":"m"}}');
    expect(r.kind).toBe("error");
  });

  it("response 无 id → -32600", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","result":1}');
    expect(r.kind).toBe("error");
  });

  it("UTF-8 中文 payload 保真", () => {
    const r = decodeFrame('{"jsonrpc":"2.0","id":"r1","method":"scene.create_node","params":{"name":"立方体"}}');
    expect(r.kind).toBe("request");
    if (r.kind === "request") {
      expect((r.request.params as { name: string }).name).toBe("立方体");
    }
  });

  it("长数字 id（规避 double 陷阱的 string id）", () => {
    const big = "123456789012345678901234567890";
    const r = decodeFrame(`{"jsonrpc":"2.0","id":"${big}","method":"x"}`);
    expect(r.kind).toBe("request");
    if (r.kind === "request") {
      expect(r.request.id).toBe(big);
    }
  });
});

describe("encodeSuccess / encodeError", () => {
  it("成功响应编码", () => {
    expect(encodeSuccess("r1", { ok: true })).toBe('{"jsonrpc":"2.0","id":"r1","result":{"ok":true}}');
  });

  it("错误响应编码（含 data.code）", () => {
    expect(encodeError("r1", { code: -32000, message: "业务失败", data: { code: "no_scene" } })).toBe(
      '{"jsonrpc":"2.0","id":"r1","error":{"code":-32000,"message":"业务失败","data":{"code":"no_scene"}}}',
    );
  });
});
