import { beforeEach, describe, expect, it, vi } from "vitest";
import {
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

  it("超时 reject { code: 'timeout' } 且迟到的应答被丢弃", async () => {
    vi.useFakeTimers();
    const { bridge, invoked, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const p = invoke<unknown>("scene.get_node_count", {}, 100);
    const args = JSON.parse(invoked[0].argsJson) as { req_id: string };
    vi.advanceTimersByTime(150);
    await expect(p).rejects.toEqual({ code: "timeout", message: expect.stringContaining("超时") });
    // 迟到应答：pending 已清，不得 resolve 已 reject 的 Promise（不抛异常即可）
    emit("method_result", JSON.stringify({ req_id: args.req_id, ok: true, result: 1 }));
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

  it("非 JSON 载荷按原字符串透传", () => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const fn = vi.fn();
    onEvent<string>("editor.some_event", fn);
    emit("editor.some_event", "plain");
    expect(fn).toHaveBeenCalledWith("plain");
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
