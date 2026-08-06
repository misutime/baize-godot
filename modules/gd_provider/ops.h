// SPDX-License-Identifier: MIT
#pragma once

/**************************************************************************/
/*  ops.h                                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "core/string/ustring.h"
#include "core/variant/variant.h"

class Node3D;

// Ops 层：能力实现——把引擎既有 API（EditorInterface/EditorSelection/
// EditorUndoRedoManager/Node）组合成语义操作，含类型转换/路径守卫/只读拒绝。
// 引擎仍是唯一执行者：Ops 只做组合/契约/防护，不重写引擎功能。
// handler 为 Registry 兼容的薄转发（Dictionary 参数 → 类型化实现）。
class Ops {
public:
	// Registry handler（薄转发）。
	static Dictionary h_get_state(const Dictionary &p_args);
	static Dictionary h_select_node(const Dictionary &p_args);
	static Dictionary h_undo(const Dictionary &p_args);
	static Dictionary h_redo(const Dictionary &p_args);
	static Dictionary h_get_node_position(const Dictionary &p_args);
	static Dictionary h_set_node_position(const Dictionary &p_args);

private:
	// 统一返回语义（与 TS 契约对齐）。
	static Dictionary _ok(const Variant &p_result);
	static Dictionary _err(const String &p_code, const String &p_message);

	/// 解析场景相对路径到 Node3D（"." = 根；禁止绝对路径/..；必须是根自身或子孙）。
	/// 失败时返回 null 并填充 r_err（{ok:false,error} 语义）。
	static Node3D *_resolve_node3d(const String &p_path, Dictionary &r_err);
};
