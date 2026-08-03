import { beforeEach, describe, expect, it } from "vitest";
import { editor, scene } from "./index";
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

// 表驱动：shipped 的 scene/editor 注册表与协议 §3.3 一一对应（防错绑协议名）。
// 注：事件注册表类型用别名而非内联嵌套箭头类型——esbuild(vitest transform) 对
// `as Array<[string, (l: (p: unknown) => void) => () => void]>(` 的解析会误报。
type MethodTableEntry = [string, () => Promise<unknown>];
type EventTableEntry = [string, (listener: (payload: unknown) => void) => () => void];

describe("shipped 方法注册表", () => {
  it.each([
    ["scene.get_node_count", () => scene.getNodeCount()],
    ["scene.create_node", () => scene.createNode({ name: "WebNode" })],
    ["scene.get_node_position", () => scene.getNodePosition({ node_path: "WebNode" })],
    [
      "scene.set_node_position",
      () => scene.setNodePosition({ node_path: "WebNode", position: { x: 1, y: 2, z: 3 } }),
    ],
    ["editor.undo", () => editor.undo()],
    ["editor.redo", () => editor.redo()],
    ["editor.get_ui_font_size", () => editor.getUiFontSize()],
    ["editor.get_ui_scale", () => editor.getUiScale()],
    ["editor.get_ui_font", () => editor.getUiFont()],
    ["editor.get_ui_font_bold", () => editor.getUiFontBold()],
  ] as MethodTableEntry[])("方法 %s 发出正确协议名", (protocolName, call) => {
    const { bridge, invoked } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    call();
    expect(invoked[0].method).toBe(protocolName);
    expect(JSON.parse(invoked[0].argsJson)).toHaveProperty("req_id");
  });
});

describe("shipped 事件注册表", () => {
  it.each([
    ["editor.selection_changed", editor.onSelectionChanged],
    ["editor.node_position_changed", editor.onPositionChanged],
    ["editor.undo_stack_changed", editor.onUndoStackChanged],
    ["editor.ui_font_size_changed", editor.onUiFontSizeChanged],
    ["editor.ui_font_changed", editor.onUiFontChanged],
  ] as EventTableEntry[])("事件 %s 订阅/退订", (protocolName, subscribe) => {
    const { bridge, emit } = makeFakeBridge();
    _setBridgeClientForTest(bridge);
    const received: unknown[] = [];
    const unsub = subscribe((payload) => received.push(payload));
    emit(protocolName, JSON.stringify({ ok: true }));
    expect(received).toHaveLength(1);
    unsub();
    emit(protocolName, JSON.stringify({ ok: true }));
    expect(received).toHaveLength(1); // 退订后不再收到
  });
});
