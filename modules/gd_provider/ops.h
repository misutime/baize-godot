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

class Node;
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
	static Dictionary h_get_tree(const Dictionary &p_args);
	static Dictionary h_get_props(const Dictionary &p_args);
	static Dictionary h_set_prop(const Dictionary &p_args);
	static Dictionary h_create_node(const Dictionary &p_args);
	static Dictionary h_remove_node(const Dictionary &p_args);
	static Dictionary h_save_scene(const Dictionary &p_args);
	static Dictionary h_save_scene_as(const Dictionary &p_args);
	static Dictionary h_get_theme(const Dictionary &p_args);
	static Dictionary h_get_scale(const Dictionary &p_args);
	static Dictionary h_get_project_info(const Dictionary &p_args);

	/// 序列化编辑场景根为 TreeNode（path 用场景根 get_path_to 的结果；根固定 "."）。
	/// ProviderServer 的 scene.changed 事件复用同一实现，避免重复序列化逻辑。
	static Dictionary serialize_tree(Node *p_scene_root);

private:
	// 统一返回语义（与 TS 契约对齐）。
	static Dictionary _ok(const Variant &p_result);
	static Dictionary _err(const String &p_code, const String &p_message);

	/// 解析场景相对路径到 Node3D（"." = 根；禁止绝对路径/..；必须是根自身或子孙）。
	/// 失败时返回 null 并填充 r_err（{ok:false,error} 语义）。
	static Node3D *_resolve_node3d(const String &p_path, Dictionary &r_err);

	/// 解析场景相对路径到任意 Node（同样的路径守卫：禁绝对路径/.. + 根归属校验）。
	/// get_props/set_prop/create_node(父)/remove_node 使用；失败返回 null 并填充 r_err。
	static Node *_resolve_node(const String &p_path, Dictionary &r_err);

	/// 递归序列化 TreeNode（p_node 自身；path 由 p_scene_root 的 get_path_to 给出）。
	static Dictionary _tree_dict(Node *p_node, Node *p_scene_root);

	/// 按契约编码表把 Variant 转为可 JSON 编码结构（NIL→null / 数学类型→{x,y,...}）。
	/// 不可编码类型返回 false（get_props 记为 value:null + editable:false；set_prop 拒绝）。
	static bool _encode_value(const Variant &p_value, Variant &r_out);

	/// 按属性类型从 JSON 结构解码回 Variant（INT 可作 FLOAT 源；非法结构返回 false）。
	static bool _decode_value(const Variant &p_value, Variant::Type p_type, Variant &r_out);
};
