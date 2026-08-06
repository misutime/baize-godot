/**
 * @baize/godot-sdk：能力面客户端（方法绑定 + 事件订阅 + react hooks）。
 *
 * 用法：createClient(transport) → { scene, editor, ... }——transport 由调用方注入：
 * - Electron 渲染进程：@baize/godot-rpc 的 createIpcTransport（经主进程转发）；
 * - Node CLI / AI：createWsTransport（直连 Godot WS）；
 * - 未来 QuickJS 脚本：inproc transport（进程内直调）。
 *
 * 方法/事件清单当前为骨架（scene.* 与 editor.* 沿用既有语义）；Godot Provider 的
 * Catalog 定案后由 schema 生成替换（保持签名一致）。
 */
import type { Transport } from "@baize/godot-rpc";

import { defineEvent, defineMethod, type EmptyParams } from "./registry";

export { defineEvent, defineMethod } from "./registry";
export type { EmptyParams } from "./registry";

// ---- 能力 payload 类型 ----

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface SelectionChangedPayload {
  node_paths: string[];
}

export interface PositionChangedPayload {
  /** 场景相对路径（与 C++ Provider 事件契约一致）。 */
  node_path: string;
  position: Vec3;
}

export interface UndoStackChangedPayload {
  can_undo: boolean;
  can_redo: boolean;
}

export interface SceneChangedPayload {
  /** 是否有编辑场景根（false = 无打开场景）。 */
  has_scene: boolean;
  /** 当前场景文件路径（新建未保存场景为空串）。 */
  scene_path: string;
}

export interface EditorStatePayload {
  has_scene: boolean;
  selection: string[];
  can_undo: boolean;
  can_redo: boolean;
}

/** 客户端实例：能力方法 + 事件订阅（绑定给定 transport）。 */
export interface GodotClient {
  scene: {
    get_node_position: (params: { node_path: string }) => Promise<Vec3>;
    set_node_position: (params: { node_path: string; position: Vec3 }) => Promise<Record<string, never>>;
  };
  editor: {
    get_state: () => Promise<EditorStatePayload>;
    select_node: (params: { node_path: string }) => Promise<Record<string, never>>;
    undo: () => Promise<Record<string, never>>;
    redo: () => Promise<Record<string, never>>;
    on_selection_changed: (listener: (payload: SelectionChangedPayload) => void) => () => void;
    on_position_changed: (listener: (payload: PositionChangedPayload) => void) => () => void;
    on_undo_stack_changed: (listener: (payload: UndoStackChangedPayload) => void) => () => void;
    on_scene_changed: (listener: (payload: SceneChangedPayload) => void) => () => void;
  };
  /** 底层 transport（进阶用途：自定义方法/协议级调用）。 */
  transport: Transport;
}

/** 创建能力面客户端实例（注入 transport）。 */
export function createClient(transport: Transport): GodotClient {
  return {
    scene: {
      get_node_position: defineMethod<{ node_path: string }, Vec3>(transport, "scene.get_node_position"),
      set_node_position: defineMethod<{ node_path: string; position: Vec3 }, Record<string, never>>(
        transport,
        "scene.set_node_position",
      ),
    },
    editor: {
      get_state: defineMethod<EmptyParams, EditorStatePayload>(transport, "editor.get_state"),
      select_node: defineMethod<{ node_path: string }, Record<string, never>>(transport, "editor.select_node"),
      undo: defineMethod<EmptyParams, Record<string, never>>(transport, "editor.undo"),
      redo: defineMethod<EmptyParams, Record<string, never>>(transport, "editor.redo"),
      on_selection_changed: defineEvent<SelectionChangedPayload>(transport, "editor.selection_changed"),
      on_position_changed: defineEvent<PositionChangedPayload>(transport, "editor.node_position_changed"),
      on_undo_stack_changed: defineEvent<UndoStackChangedPayload>(transport, "editor.undo_stack_changed"),
      on_scene_changed: defineEvent<SceneChangedPayload>(transport, "editor.scene_changed"),
    },
    transport,
  };
}
