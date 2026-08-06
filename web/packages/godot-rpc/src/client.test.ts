/**
 * RpcClient 配对语义：超时/迟到应答丢弃/通知下行/断线 failAllPending。
 */
import { describe, expect, it, vi } from "vitest";

import { RpcCallError, RpcClient, RpcTimeoutError } from "./client";

function frame(obj: unknown): string {
  return JSON.stringify(obj);
}

describe("RpcClient 配对", () => {
  it("invoke → 发送 request 帧（string id + jsonrpc 2.0）", async () => {
    const sent: string[] = [];
    const client = new RpcClient((t) => sent.push(t));
    const p = client.invoke("hello", { token: "t" });
    expect(sent).toHaveLength(1);
    expect(JSON.parse(sent[0])).toMatchObject({ jsonrpc: "2.0", method: "hello", params: { token: "t" } });
    client.handleFrame(frame({ jsonrpc: "2.0", id: JSON.parse(sent[0]).id, result: { ok: true } }));
    await expect(p).resolves.toEqual({ ok: true });
  });

  it("错误响应 → RpcCallError（含 code/data）", async () => {
    const sent: string[] = [];
    const client = new RpcClient((t) => sent.push(t));
    const p = client.invoke("scene.create_node", { name: "x" });
    const id = JSON.parse(sent[0]).id;
    client.handleFrame(
      frame({ jsonrpc: "2.0", id, error: { code: -32000, message: "无场景", data: { code: "no_scene" } } }),
    );
    await expect(p).rejects.toMatchObject({ name: "RpcCallError", code: -32000, data: { code: "no_scene" } });
  });

  it("超时 → RpcTimeoutError（悬空防护）", async () => {
    vi.useFakeTimers();
    try {
      const client = new RpcClient(() => {});
      const p = client.invoke("health", undefined, 100);
      vi.advanceTimersByTime(150);
      await expect(p).rejects.toBeInstanceOf(RpcTimeoutError);
      expect(client.pendingCount()).toBe(0);
    } finally {
      vi.useRealTimers();
    }
  });

  it("迟到应答丢弃（pending 已清）", async () => {
    const sent: string[] = [];
    const client = new RpcClient((t) => sent.push(t));
    const p = client.invoke("health");
    const id = JSON.parse(sent[0]).id;
    client.handleFrame(frame({ jsonrpc: "2.0", id, result: "first" }));
    await expect(p).resolves.toBe("first");
    // 迟到重复应答：无配对目标，静默丢弃（不抛、不 resolve）
    expect(() => client.handleFrame(frame({ jsonrpc: "2.0", id, result: "late" }))).not.toThrow();
    expect(client.pendingCount()).toBe(0);
  });

  it("通知下行 → onNotification 监听器", () => {
    const client = new RpcClient(() => {});
    const seen: Array<[string, unknown]> = [];
    const unsub = client.onNotification((method, params) => seen.push([method, params]));
    client.handleFrame(frame({ jsonrpc: "2.0", method: "editor.selection_changed", params: { node_paths: [] } }));
    expect(seen).toEqual([["editor.selection_changed", { node_paths: [] }]]);
    unsub();
    client.handleFrame(frame({ jsonrpc: "2.0", method: "editor.selection_changed", params: {} }));
    expect(seen).toHaveLength(1);
  });

  it("断线 failAllPending 以稳定错误拒绝全部", async () => {
    const client = new RpcClient(() => {});
    const p1 = client.invoke("a");
    const p2 = client.invoke("b");
    client.failAllPending("连接关闭");
    await expect(p1).rejects.toThrow("连接关闭");
    await expect(p2).rejects.toThrow("连接关闭");
    expect(client.pendingCount()).toBe(0);
  });

  it("dispose 后 invoke 确定性拒绝", async () => {
    const client = new RpcClient(() => {});
    client.dispose();
    await expect(client.invoke("a")).rejects.toThrow("dispose");
  });

  it("RpcCallError 保留 code/message", () => {
    const err = new RpcCallError(-32601, "Method not found");
    expect(err).toBeInstanceOf(Error);
    expect(err.message).toBe("Method not found");
    expect(err.code).toBe(-32601);
  });
});
