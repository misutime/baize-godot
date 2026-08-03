import { beforeEach, describe, expect, it } from "vitest";
import { defineEvent, defineMethod } from "./registry";
import { _resetTransportForTest, _setBridgeClientForTest, type CefViewClientLike } from "./transport";

function makeFakeBridge() {
  const listeners = new Map<string, Array<(payloadJson: string) => void>>();
  const invoked: Array<{ method: string; argsJson: string }> = [];
  const bridge: CefViewClientLike = {
    invoke: (method, argsJson) => {
      invoked.push({ method, argsJson });
    },
    addEventListener: (type, listener) => {
      const list = listeners.get(type) ?? [];
      list.push(listener);
      listeners.set(type, list);
    },
    removeEventListener: (type, listener) => {
      const list = listeners.get(type) ?? [];
      listeners.set(
        type,
        list.filter((l) => l !== listener),
      );
    },
  };
  const emit = (type: string, payloadJson: string): void => {
    for (const listener of listeners.get(type) ?? []) {
      listener(payloadJson);
    }
  };
  return { bridge, invoked, emit };
}

beforeEach(() => {
  _resetTransportForTest();
  _setBridgeClientForTest(null);
});

describe("defineMethod", () => {
  it("映射到协议方法名并配对应答（create_node 场景）", async () => {
    const { bridge, invoked, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const createNode = defineMethod<{ name: string }, number>("scene.create_node");
    const p = createNode({ name: "WebNode" });
    expect(invoked[0].method).toBe("scene.create_node");
    const args = JSON.parse(invoked[0].argsJson) as { req_id: string; name: string };
    expect(args.name).toBe("WebNode");
    emit("method_result", JSON.stringify({ req_id: args.req_id, ok: true, result: 42 }));
    await expect(p).resolves.toBe(42);
  });

  it("自定义超时透传", async () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const undo = defineMethod<Record<string, never>, Record<string, never>>("editor.undo");
    const p = undo({}, 1); // 1ms 超时
    await expect(p).rejects.toEqual({ code: "timeout", message: expect.stringContaining("超时") });
    emit("method_result", '{"req_id":"x","ok":true}'); // 迟到应答不抛异常
  });
});

describe("defineEvent", () => {
  it("映射到协议事件名并解析 payload", () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const onSelectionChanged = defineEvent<{ node_paths: string[] }>("editor.selection_changed");
    const received: Array<{ node_paths: string[] }> = [];
    const unsub = onSelectionChanged((payload) => received.push(payload));
    emit("editor.selection_changed", JSON.stringify({ node_paths: ["Node3D", "."] }));
    expect(received).toEqual([{ node_paths: ["Node3D", "."] }]);
    unsub();
  });
});
