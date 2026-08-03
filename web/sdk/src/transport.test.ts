import { beforeEach, describe, expect, it, vi } from "vitest";
import {
  _pendingCountForTest,
  _resetTransportForTest,
  _setBridgeClientForTest,
  type CefViewClientLike,
  getBridgeClient,
  handleMethodResult,
  invoke,
  onEvent,
} from "./transport";

// 可控假桥：记录 invoke 调用 + 事件监听器表（模拟 CefViewCore 注入的 window.CefViewClient）。
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
  vi.useRealTimers();
});

describe("invoke", () => {
  it("发出正确协议字符串：方法名 + JSON 载荷（req_id 为字符串，参数合并）", async () => {
    const { bridge, invoked, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const p = invoke<{ x: number; y: number; z: number }>("scene.set_node_position", {
      node_path: "WebNode",
      position: { x: 5, y: 6, z: 7 },
    });
    expect(invoked).toHaveLength(1);
    expect(invoked[0].method).toBe("scene.set_node_position");
    const args = JSON.parse(invoked[0].argsJson) as Record<string, unknown>;
    expect(typeof args.req_id).toBe("string");
    expect(args.node_path).toBe("WebNode");
    expect(args.position).toEqual({ x: 5, y: 6, z: 7 });
    // 应答配对（req_id 一致才 resolve）
    emit("method_result", JSON.stringify({ req_id: args.req_id, ok: true, result: { x: 5, y: 6, z: 7 } }));
    await expect(p).resolves.toEqual({ x: 5, y: 6, z: 7 });
  });

  it("并发请求乱序应答仍按 req_id 正确配对（不总是 settle 第一个）", async () => {
    const { bridge, invoked, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const p1 = invoke<number>("scene.get_node_count", {});
    const p2 = invoke<string>("scene.create_node", { name: "WebNode" });
    const id1 = (JSON.parse(invoked[0].argsJson) as { req_id: string }).req_id;
    const id2 = (JSON.parse(invoked[1].argsJson) as { req_id: string }).req_id;
    expect(id1).not.toBe(id2);
    // 乱序：先应答第二个请求；两个 pending 期间来一个未知 req_id
    emit("method_result", JSON.stringify({ req_id: id2, ok: true, result: "node-2" }));
    emit("method_result", JSON.stringify({ req_id: "stranger", ok: true, result: "x" }));
    emit("method_result", JSON.stringify({ req_id: id1, ok: true, result: 7 }));
    await expect(p2).resolves.toBe("node-2");
    await expect(p1).resolves.toBe(7);
    expect(_pendingCountForTest()).toBe(0);
  });

  it("错误应答 reject BridgeError（code/message 透传）", async () => {
    const { bridge, invoked, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const p = invoke<unknown>("scene.get_node_position", { node_path: "Missing" });
    const args = JSON.parse(invoked[0].argsJson) as { req_id: string };
    emit(
      "method_result",
      JSON.stringify({ req_id: args.req_id, ok: false, error: { code: "invalid_node", message: "找不到" } }),
    );
    await expect(p).rejects.toEqual({ code: "invalid_node", message: "找不到" });
  });

  it("超时 reject { code: 'timeout' } 且 pending 清理可观测、迟到应答被丢弃", async () => {
    vi.useFakeTimers();
    const { bridge, invoked, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const p = invoke<unknown>("scene.get_node_count", {}, 100);
    const args = JSON.parse(invoked[0].argsJson) as { req_id: string };
    expect(_pendingCountForTest()).toBe(1); // 超时前挂起
    vi.advanceTimersByTime(150);
    await expect(p).rejects.toEqual({ code: "timeout", message: expect.stringContaining("超时") });
    expect(_pendingCountForTest()).toBe(0); // 超时即从 pending 移除（不泄漏）
    // 迟到应答：pending 已空，不得复活/误配
    emit("method_result", JSON.stringify({ req_id: args.req_id, ok: true, result: 1 }));
    expect(_pendingCountForTest()).toBe(0);
  });

  it("桥注入缺失显式报错（不静默回退）", async () => {
    expect(() => invoke("scene.get_node_count", {})).toThrow(/CefViewClient bridge not available/);
  });

  it("未知 req_id 应答防御性丢弃（不抛异常）", () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    expect(() =>
      emit("method_result", JSON.stringify({ req_id: "nope", ok: true, result: 1 })),
    ).not.toThrow();
  });

  it("非 JSON 载荷防御性丢弃", () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    expect(() => emit("method_result", "not json")).not.toThrow();
  });
});

describe("onEvent", () => {
  it("订阅 → JSON 载荷解析为对象；退订后不再触发", () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const fn = vi.fn();
    const unsub = onEvent<{ node_paths: string[] }>("editor.selection_changed", fn);
    emit("editor.selection_changed", JSON.stringify({ node_paths: ["Node3D"] }));
    expect(fn).toHaveBeenCalledWith({ node_paths: ["Node3D"] });
    unsub();
    emit("editor.selection_changed", JSON.stringify({ node_paths: ["Other"] }));
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it("非 JSON 事件载荷显式上报（console.error）且不调用业务监听器", () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const errSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    const fn = vi.fn();
    onEvent<{ a: number }>("editor.some_event", fn);
    emit("editor.some_event", "not json");
    expect(fn).not.toHaveBeenCalled();
    expect(errSpy).toHaveBeenCalled();
    errSpy.mockRestore();
  });
});

describe("getBridgeClient", () => {
  it("测试注入优先于 window 探测", () => {
    const { bridge } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    expect(getBridgeClient()).toBe(bridge);
  });

  it("handleMethodResult 直接调用（内部通道测试）", () => {
    // 无 pending 时静默；不抛异常
    expect(() => handleMethodResult('{"req_id":"x","ok":true,"result":1}')).not.toThrow();
  });
});
