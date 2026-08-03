/**************************************************************************/
/*  editor_ui_tree.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

class Control;
class Node;
class Tree;
class TreeItem;

// 编辑器语义 UI 树导出（AI FIRST P1，方案《02-方案-语义化AI接口层.md》§2）。
//
// 遍历 EditorNode 的 Control 树，导出 role/name/state/children 语义快照：
// AI 感知编辑器界面 = 读数据（Button.text / TreeItem 文本 / Tooltip），非 OCR/截图。
//
// 语义 ID（会话内稳定）：Control = 逐段路径（每段优先 meta "ai_name"，否则节点名）；
// TreeItem = "<树控件语义ID>/item[/<索引>:<文本>…]"（根 item 即 "…/item"）。
// 同会话内稳定，跨会话以 role+name 定位（AI 每次会话重新拉快照）。
class EditorUiTree {
public:
	/// 完整编辑器 UI 树快照（Dictionary → JSON：{ id, role, name, state, items?, children }）。
	static Dictionary export_tree();

	/// 按语义 ID 解析回 Control：逐段匹配 ai_name 优先、节点名其次，回退 NodePath。
	/// 拒绝 ".." 穿越；失败返回 nullptr。
	static Control *find_control(const String &p_id);

	/// 在 Tree 内按 item 路径（"…/item" 或 "…/item/0:name/1:sub"）查找 TreeItem。
	static TreeItem *find_tree_item(Tree *p_tree, const String &p_item_path);

private:
	static void _export_control(Node *p_node, Dictionary &r_node);
	/// Control 语义 ID：逐段路径（ai_name 优先），段内净化分隔符。
	static String _semantic_id(Control *p_control);
	/// Control 类型 → 语义 role（button / text_field / tree / tree_item / select / ...）。
	static String _role_for(Control *p_control);
	/// 可访问名：meta "ai_name" > 控件文本（Button/Label 等）> Tooltip > 类名。
	static String _name_for(Control *p_control);
	/// 控件状态字典（enabled/visible/focused/selected/pressed/value/editable/secret）。
	static Dictionary _state_for(Control *p_control);
	/// 递归导出 Tree 的 TreeItem（TreeItem 非 Node，需特殊遍历）；根 item 地址 = base。
	static void _export_tree_items(TreeItem *p_item, Array &r_items, const String &p_base_path, bool p_is_root);
};
