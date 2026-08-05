/**************************************************************************/
/*  ui_tree.cpp                                                    */
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

#include "ui_tree.h"

#ifdef TOOLS_ENABLED

#include "editor/editor_node.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_bar.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"

// 语义 UI 树导出实现（AI FIRST P1）。

// 段名规范化（导出与查找共用同一编码，保证可逆）：净化路径/索引分隔符与保留段。
static String _encode_segment(const String &p_seg) {
	String seg = p_seg.strip_edges().replace("/", "_").replace(":", "_");
	if (seg == "." || seg == ".." || seg.is_empty()) {
		seg = "_";
	}
	return seg;
}
//
// 语义 ID：逐段路径，每段优先取 meta "ai_name"（如 "scene_tree"），否则节点名；
// 段内净化 "/" 与 ":"（路径分隔符/索引分隔符）。未打语义名的控件 ID 与 NodePath
// 一致（兼容），P5 打标后 ID 自动语义化——不再需要改协议。解析（find_control）
// 按段匹配 ai_name 优先、节点名其次，两者皆失败回退 NodePath。
//
// TreeItem 地址（方案 §2.2 的 items 为控件字典的兄弟字段，非 state 内）：
//   "<树控件语义ID>/item"                = 树的根 item（叶子根场景节点也可寻址）
//   "<树控件语义ID>/item/<索引>:<文本>/…" = 逐层子索引（文本仅作可读锚）

Dictionary EditorUiTree::export_tree() {
	Dictionary root;
	EditorNode *ed = EditorNode::get_singleton();
	if (!ed) {
		root["error"] = "EditorNode not ready";
		return root;
	}
	Dictionary node;
	node["id"] = ".";
	node["role"] = "editor";
	node["name"] = "EditorNode";
	node["state"] = Dictionary();
	Array children;
	// EditorNode 的直接子节点统一作为子导出（_export_control 内部区分 Control/容器）。
	for (int i = 0; i < ed->get_child_count(); i++) {
		Dictionary sub;
		_export_control(ed->get_child(i), sub);
		children.append(sub);
	}
	node["children"] = children;
	root["ui"] = node;
	return root;
}

Control *EditorUiTree::find_control(const String &p_id) {
	EditorNode *ed = EditorNode::get_singleton();
	if (!ed) {
		return nullptr;
	}
	if (p_id.is_empty() || p_id == ".") {
		return nullptr; // 根是 EditorNode（Node 派生，非 Control）：不可作为操作目标
	}
	// 语义路径：逐段匹配（ai_name 优先，其次节点名）。
	Vector<String> segs = p_id.split("/");
	Node *cur = ed;
	for (const String &seg : segs) {
		if (seg.is_empty() || seg == ".") {
			continue;
		}
		if (seg == "..") {
			return nullptr; // 拒绝父级穿越
		}
		Node *next = nullptr;
		for (int i = 0; i < cur->get_child_count(); i++) {
			Node *c = cur->get_child(i);
			if (c->has_meta("ai_name")) {
				const String an = _encode_segment(c->get_meta("ai_name").operator String());
				if (an == seg) {
					next = c;
					break;
				}
			}
		}
		if (!next) {
			for (int i = 0; i < cur->get_child_count(); i++) {
				Node *c = cur->get_child(i);
				if (_encode_segment(String(c->get_name())) == seg) {
					next = c;
					break;
				}
			}
		}
		if (!next) {
			return nullptr;
		}
		cur = next;
	}
	if (Object::cast_to<Control>(cur)) {
		return Object::cast_to<Control>(cur);
	}
	// 回退：NodePath 直解（含转义路径等语义段解析不到的情况）。
	Node *n = ed->get_node_or_null(NodePath(p_id));
	return Object::cast_to<Control>(n);
}

TreeItem *EditorUiTree::find_tree_item(Tree *p_tree, const String &p_item_path) {
	// 格式："…/item"（根自身）或 "…/item/<索引>:<文本>/…"（逐层子索引）。
	if (!p_tree) {
		return nullptr;
	}
	Vector<String> parts = p_item_path.split("/");
	TreeItem *item = p_tree->get_root();
	for (const String &part : parts) {
		if (part.is_empty() || part == "item") {
			continue;
		}
		const int sep = part.find(":");
		const String idx_str = sep >= 0 ? part.substr(0, sep) : part;
		if (!idx_str.is_valid_int()) {
			return nullptr;
		}
		const int index = idx_str.to_int();
		if (index < 0) {
			return nullptr;
		}
		TreeItem *child = item ? item->get_first_child() : nullptr;
		int i = 0;
		while (child && i < index) {
			child = child->get_next();
			i++;
		}
		if (!child || i != index) {
			return nullptr; // 索引越界
		}
		item = child;
	}
	return item;
}

String EditorUiTree::_semantic_id(Control *p_control) {
	EditorNode *ed = EditorNode::get_singleton();
	Vector<String> segs;
	Node *cur = p_control;
	while (cur && cur != ed) {
		String seg = String(cur->get_name());
		if (cur->has_meta("ai_name")) {
			const String an = cur->get_meta("ai_name").operator String().strip_edges();
			if (!an.is_empty()) {
				seg = an;
			}
		}
		segs.push_back(_encode_segment(seg));
		cur = cur->get_parent();
	}
	if (cur != ed) {
		return String(ed->get_path_to(p_control)); // 不在 EditorNode 树下：回退 NodePath
	}
	segs.reverse(); // 自底向上收集，反转成从根到叶
	return String("/").join(segs);
}

void EditorUiTree::_export_control(Node *p_node, Dictionary &r_node) {
	Control *ctrl = Object::cast_to<Control>(p_node);
	if (ctrl) {
		r_node["id"] = _semantic_id(ctrl);
		r_node["role"] = _role_for(ctrl);
		r_node["name"] = _name_for(ctrl);
		r_node["state"] = _state_for(ctrl);
		// 方案 §2.2：Tree 的 items 是控件字典的兄弟字段（与 state 平级）。
		if (const Tree *tree = Object::cast_to<Tree>(ctrl)) {
			Array items;
			TreeItem *root = tree->get_root();
			if (root) {
				_export_tree_items(root, items, r_node["id"].operator String() + "/item", true);
			}
			r_node["items"] = items;
		}
		Array children;
		for (int i = 0; i < ctrl->get_child_count(); i++) {
			Node *child = ctrl->get_child(i);
			if (!Object::cast_to<Control>(child)) {
				continue; // 只导出 Control 层
			}
			Dictionary sub;
			_export_control(child, sub);
			children.append(sub);
		}
		r_node["children"] = children;
		return;
	}
	// 非 Control 节点（容器）递归到子 Control。
	Array children;
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		Dictionary sub;
		_export_control(child, sub);
		children.append(sub);
	}
	r_node["role"] = "container";
	r_node["children"] = children;
}

String EditorUiTree::_role_for(Control *p_control) {
	if (Object::cast_to<Button>(p_control)) {
		if (Object::cast_to<CheckBox>(p_control) || Object::cast_to<CheckButton>(p_control)) {
			return "checkbox";
		}
		if (Object::cast_to<OptionButton>(p_control)) {
			return "select";
		}
		if (Object::cast_to<MenuButton>(p_control)) {
			return "menu_button";
		}
		if (Object::cast_to<ColorPickerButton>(p_control)) {
			return "color_button";
		}
		return "button";
	}
	// 直接继承 BaseButton 的控件（LinkButton/TextureButton 等）也是可激活按钮。
	if (Object::cast_to<BaseButton>(p_control)) {
		return "button";
	}
	if (Object::cast_to<LineEdit>(p_control)) {
		return "text_field";
	}
	if (Object::cast_to<TextEdit>(p_control)) {
		return "text_area";
	}
	if (Object::cast_to<SpinBox>(p_control)) {
		return "spinbox";
	}
	if (Object::cast_to<Tree>(p_control)) {
		return "tree";
	}
	if (Object::cast_to<TabBar>(p_control)) {
		return "tab";
	}
	if (Object::cast_to<MenuBar>(p_control)) {
		return "menu";
	}
	if (Object::cast_to<Label>(p_control)) {
		return "text";
	}
	if (Object::cast_to<Panel>(p_control)) {
		return "panel";
	}
	return "container";
}

String EditorUiTree::_name_for(Control *p_control) {
	if (p_control->has_meta("ai_name")) {
		return p_control->get_meta("ai_name").operator String();
	}
	if (Object::cast_to<Button>(p_control)) {
		String t = Object::cast_to<Button>(p_control)->get_text();
		if (!t.is_empty()) {
			return t;
		}
	}
	if (Object::cast_to<Label>(p_control)) {
		String t = Object::cast_to<Label>(p_control)->get_text();
		if (!t.is_empty()) {
			return t;
		}
	}
	if (Object::cast_to<LineEdit>(p_control)) {
		String t = Object::cast_to<LineEdit>(p_control)->get_placeholder();
		if (!t.is_empty()) {
			return t;
		}
	}
	String tip = p_control->get_tooltip_text();
	if (!tip.is_empty() && tip != p_control->get_class()) {
		return tip;
	}
	return p_control->get_name();
}

Dictionary EditorUiTree::_state_for(Control *p_control) {
	Dictionary state;
	// Control 无 is_disabled（BaseButton 才有）；非按钮默认 enabled。
	bool disabled = false;
	if (const BaseButton *bb = Object::cast_to<BaseButton>(p_control)) {
		disabled = bb->is_disabled();
	}
	state["enabled"] = !disabled;
	state["visible"] = p_control->is_visible_in_tree();
	state["focused"] = p_control->has_focus();
	if (const Button *btn = Object::cast_to<Button>(p_control)) {
		state["pressed"] = btn->is_pressed();
	}
	if (const CheckBox *cb = Object::cast_to<CheckBox>(p_control)) {
		state["checked"] = cb->is_pressed();
	}
	if (const LineEdit *le = Object::cast_to<LineEdit>(p_control)) {
		state["editable"] = le->is_editable();
		if (le->is_secret()) {
			state["secret"] = true; // 凭据类字段：不导出明文值
		} else {
			state["value"] = le->get_text();
		}
	}
	if (const TextEdit *te = Object::cast_to<TextEdit>(p_control)) {
		state["editable"] = te->is_editable();
		state["value"] = te->get_text();
	}
	if (const SpinBox *sb = Object::cast_to<SpinBox>(p_control)) {
		state["editable"] = sb->is_editable();
		state["value"] = sb->get_value();
	}
	if (const OptionButton *ob = Object::cast_to<OptionButton>(p_control)) {
		const int sel = ob->get_selected();
		state["selected_index"] = sel;
		// get_selected() 无选中项时为 -1，不能直接喂 get_item_text（引擎报错）。
		state["value"] = (sel >= 0 && sel < ob->get_item_count()) ? ob->get_item_text(sel) : String();
	}
	if (const Tree *tree = Object::cast_to<Tree>(p_control)) {
		TreeItem *sel = tree->get_selected();
		if (sel) {
			state["selected"] = sel->get_text(0);
		}
	}
	return state;
}

void EditorUiTree::_export_tree_items(TreeItem *p_item, Array &r_items, const String &p_base_path, bool p_is_root) {
	// 导出 p_item 自身（root item 也可能是场景根节点 3dMain——只遍历其子会漏掉叶子根）。
	Dictionary d;
	// 文本中的 "/" 会切成新路径段（解析器按段取索引），编码为 %2F 保证 ID 可往返。
	const String text = p_item->get_text(0).replace("/", "%2F");
	// 根 item 自身地址 = base（"…/item"，唯一）；子项 = base + "/<索引>:<文本>"。
	const String path = p_is_root ? p_base_path : p_base_path + "/" + itos(p_item->get_index()) + ":" + text;
	d["id"] = path;
	d["role"] = "tree_item";
	d["name"] = text;
	Dictionary st;
	st["selected"] = p_item->is_selected(0);
	st["collapsed"] = p_item->is_collapsed();
	d["state"] = st;
	if (p_item->get_first_child()) {
		Array children;
		for (TreeItem *child = p_item->get_first_child(); child; child = child->get_next()) {
			_export_tree_items(child, children, path, false);
		}
		d["children"] = children;
	}
	r_items.append(d);
}

#endif // TOOLS_ENABLED
