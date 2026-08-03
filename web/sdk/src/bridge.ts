// 桥方法/事件实例：与 C++ 侧 web_bridge 方法注册表一一对应（协议 §3.3）。

import { defineEvent, defineMethod, type EmptyParams } from "./registry";

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

// ---- 方法（JS→C++）----

/** 当前场景节点数。 */
export const getNodeCount = defineMethod<EmptyParams, number>("scene.get_node_count");

/** 创建 Node3D（undo 可撤销），返回 node_id（instance_id）。 */
export const createNode = defineMethod<{ name: string }, number>("scene.create_node");

/** 返回 Node3D 位置；node_path 为场景相对路径（"."=根，与 selection_changed 一致）。 */
export const getNodePosition = defineMethod<{ node_path: string }, Vec3>("scene.get_node_position");

/** 设置 Node3D 位置（undo 可撤销）。 */
export const setNodePosition = defineMethod<{ node_path: string; position: Vec3 }, Record<string, never>>(
  "scene.set_node_position",
);

/** 撤销上一步（nothing_to_undo 时 reject）。 */
export const undo = defineMethod<EmptyParams, Record<string, never>>("editor.undo");

/** 重做上一步（nothing_to_redo 时 reject）。 */
export const redo = defineMethod<EmptyParams, Record<string, never>>("editor.redo");

// ---- 事件（C++→JS）----

export interface SelectionChangedPayload {
  node_paths: string[];
}

export interface PositionChangedPayload {
  node_id: number;
  position: Vec3;
}

export interface UndoStackChangedPayload {
  can_undo: boolean;
  can_redo: boolean;
}

/** 编辑器选中变化（node_paths 为场景相对路径数组）。 */
export const onSelectionChanged = defineEvent<SelectionChangedPayload>("editor.selection_changed");

/** 选中 Node3D 位置变化（帧轮询 diff 推送）。 */
export const onPositionChanged = defineEvent<PositionChangedPayload>("editor.node_position_changed");

/** undo 栈状态变化。 */
export const onUndoStackChanged = defineEvent<UndoStackChangedPayload>("editor.undo_stack_changed");
