// SDK 主入口：类型化 API 对象（用法见协议文档 §4.2/4.3）。

export type {
  PositionChangedPayload,
  SelectionChangedPayload,
  UndoStackChangedPayload,
  Vec3,
} from "./bridge";
export {
  createNode,
  getNodeCount,
  getNodePosition,
  getUiFont,
  getUiFontBold,
  getUiFontSize,
  getUiScale,
  onPositionChanged,
  onSelectionChanged,
  onUiFontChanged,
  onUiFontSizeChanged,
  onUndoStackChanged,
  redo,
  setNodePosition,
  undo,
} from "./bridge";
export { defineEvent, defineMethod } from "./registry";
export type { BridgeError, CefViewClientLike } from "./transport";
export { getBridgeClient, invoke, onEvent } from "./transport";

import {
  createNode,
  getNodeCount,
  getNodePosition,
  getUiFont,
  getUiFontBold,
  getUiFontSize,
  getUiScale,
  onPositionChanged,
  onSelectionChanged,
  onUiFontChanged,
  onUiFontSizeChanged,
  onUndoStackChanged,
  redo,
  setNodePosition,
  undo,
} from "./bridge";

/** 场景命名空间（scene.* 协议方法）。 */
export const scene = {
  getNodeCount,
  createNode,
  getNodePosition,
  setNodePosition,
};

/** 编辑器命名空间（editor.* 协议方法/事件）。 */
export const editor = {
  undo,
  redo,
  getUiFontSize,
  getUiScale,
  getUiFont,
  getUiFontBold,
  onSelectionChanged,
  onPositionChanged,
  onUndoStackChanged,
  onUiFontSizeChanged,
  onUiFontChanged,
};
