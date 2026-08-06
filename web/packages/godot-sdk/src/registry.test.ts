/**
 * createClient/defineMethod/defineEvent 绑定语义（transport 注入式）。
 */
import { describe, expect, it } from "vitest";
import type { Transport } from "@baize/godot-rpc";

import { createClient } from "./index";

/** 假 transport：记录请求 + 可触发事件。 */
function makeFakeTransport() {
  const requests: Array<{ method: string; params: unknown; timeoutMs?: number }> = [];
  const listeners = new Set<(method: string, params: unknown) => void>();
  const transport: Transport = {
    request: async <T>(_method: string, params?: unknown, timeoutMs?: number): Promise<T> => {
      requests.push({ method: _method, params, timeoutMs });
      return { ok: true } as T;
    },
    onEvent: (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    close: () => {},
  };
  const emit = (method: string, params: unknown): void => {
    for (const l of listeners) {
      l(method, params);
    }
  };
  return { transport, requests, emit };
}

describe("createClient", () => {
  it("有参方法 → transport.request（方法名/参数透传）", async () => {
    const { transport, requests } = makeFakeTransport();
    const client = createClient(transport);
    await client.scene.create_node({ name: "Cube" });
    expect(requests).toEqual([{ method: "scene.create_node", params: { name: "Cube" }, timeoutMs: undefined }]);
  });

  it("无参方法 → 空参数对象", async () => {
    const { transport, requests } = makeFakeTransport();
    const client = createClient(transport);
    await client.scene.get_node_count();
    expect(requests[0].method).toBe("scene.get_node_count");
    expect(requests[0].params).toEqual({});
  });

  it("事件绑定 → 仅转发同名事件，退订后停止", () => {
    const { transport, emit } = makeFakeTransport();
    const client = createClient(transport);
    const seen: unknown[] = [];
    const unsub = client.editor.on_selection_changed((p) => seen.push(p));
    emit("editor.selection_changed", { node_paths: ["/root/A"] });
    emit("editor.scene_changed", { has_scene: false, scene_path: "" }); // 异名事件不转发
    expect(seen).toEqual([{ node_paths: ["/root/A"] }]);
    unsub();
    emit("editor.selection_changed", { node_paths: ["/root/B"] });
    expect(seen).toHaveLength(1);
  });

  it("transport 暴露在实例上（进阶用途）", () => {
    const { transport } = makeFakeTransport();
    const client = createClient(transport);
    expect(client.transport).toBe(transport);
  });
});
