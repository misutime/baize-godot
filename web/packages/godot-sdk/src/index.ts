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

/** 场景树节点（递归）。path 为场景相对路径："." = 根，子节点用根 get_path_to 的结果。 */
export interface TreeNode {
  /** 场景相对路径（"." = 根，子节点用根 get_path_to 的结果）。 */
  path: string;
  /** 节点名称。 */
  name: string;
  /** Godot 类名，如 "Node3D"。 */
  type: string;
  children: TreeNode[];
}

/** 节点属性信息（Inspector 用）。value 为可 JSON 编码值或 null。 */
export interface PropInfo {
  name: string;
  /** Godot Variant 类型名，如 "float"/"Vector3"。 */
  type: string;
  editable: boolean;
  value: unknown;
}

export interface SceneChangedPayload {
  /** 场景树（null = 无打开场景）。 */
  tree: TreeNode | null;
}

export interface EditorStatePayload {
  has_scene: boolean;
  selection: string[];
  can_undo: boolean;
  can_redo: boolean;
}

/** 0-1 范围 RGBA（与 Provider 值编码表 COLOR 一致）。 */
export interface Color4 {
  r: number;
  g: number;
  b: number;
  a: number;
}

/** editor.get_theme 返回：编辑器主题信息。 */
export interface ThemeInfo {
  theme_name: string;
  preset: string;
  base_color: Color4;
  accent_color: Color4;
  font_size: number;
}

/** editor.get_project_info 返回：项目信息。 */
export interface ProjectInfo {
  project_name: string;
  main_scene: string;
  rendering_method: string;
  project_path: string;
  godot_version: string;
}

/** 客户端实例：能力方法 + 事件订阅（绑定给定 transport）。 */
export interface GodotClient {
  scene: {
    /** 返回场景树根节点；无打开场景返回 null（非错误，与 scene.changed 事件语义一致）。 */
    get_tree: () => Promise<TreeNode | null>;
    get_props: (params: { node_path: string }) => Promise<PropInfo[]>;
    set_prop: (params: { node_path: string; prop: string; value: unknown }) => Promise<Record<string, never>>;
    create_node: (params: {
      type: string;
      name?: string;
      parent_path?: string;
    }) => Promise<{ node_path: string }>;
    remove_node: (params: { node_path: string }) => Promise<Record<string, never>>;
    get_node_position: (params: { node_path: string }) => Promise<Vec3>;
    set_node_position: (params: { node_path: string; position: Vec3 }) => Promise<Record<string, never>>;
  };
  editor: {
    get_state: () => Promise<EditorStatePayload>;
    select_node: (params: { node_path: string }) => Promise<Record<string, never>>;
    undo: () => Promise<Record<string, never>>;
    redo: () => Promise<Record<string, never>>;
    save_scene: () => Promise<{ path: string }>;
    save_scene_as: (params: { path: string }) => Promise<{ path: string }>;
    get_theme: () => Promise<ThemeInfo>;
    get_scale: () => Promise<{ scale: number }>;
    get_project_info: () => Promise<ProjectInfo>;
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
      get_tree: defineMethod<EmptyParams, TreeNode | null>(transport, "scene.get_tree"),
      get_props: defineMethod<{ node_path: string }, PropInfo[]>(transport, "scene.get_props"),
      set_prop: defineMethod<{ node_path: string; prop: string; value: unknown }, Record<string, never>>(
        transport,
        "scene.set_prop",
      ),
      create_node: defineMethod<{ type: string; name?: string; parent_path?: string }, { node_path: string }>(
        transport,
        "scene.create_node",
      ),
      remove_node: defineMethod<{ node_path: string }, Record<string, never>>(transport, "scene.remove_node"),
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
      save_scene: defineMethod<EmptyParams, { path: string }>(transport, "editor.save_scene"),
      save_scene_as: defineMethod<{ path: string }, { path: string }>(transport, "editor.save_scene_as"),
      get_theme: defineMethod<EmptyParams, ThemeInfo>(transport, "editor.get_theme"),
      get_scale: defineMethod<EmptyParams, { scale: number }>(transport, "editor.get_scale"),
      get_project_info: defineMethod<EmptyParams, ProjectInfo>(transport, "editor.get_project_info"),
      on_selection_changed: defineEvent<SelectionChangedPayload>(transport, "editor.selection_changed"),
      on_position_changed: defineEvent<PositionChangedPayload>(transport, "editor.node_position_changed"),
      on_undo_stack_changed: defineEvent<UndoStackChangedPayload>(transport, "editor.undo_stack_changed"),
      on_scene_changed: defineEvent<SceneChangedPayload>(transport, "scene.changed"),
    },
    transport,
  };
}
