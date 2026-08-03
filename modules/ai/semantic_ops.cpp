/**************************************************************************/
/*  semantic_ops.cpp                                                      */
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

#include "semantic_ops.h"

#ifdef TOOLS_ENABLED

#include "editor_ui_tree.h"

#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/base_button.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/node.h"
#include "scene/main/viewport.h"

#include "core/input/input_event.h"
#include "core/math/math_funcs.h"
#include "core/math/rect2.h"
#include "core/object/class_db.h"
#include "scene/scene_string_names.h"

// 语义操作实现（AI FIRST P1）。返回 { ok, result } / { ok:false, error:{code,message} }。

static Dictionary _ok(const Variant &p_result) {
	Dictionary d;
	d["ok"] = true;
	d["result"] = p_result;
	return d;
}

static Dictionary _err(const String &p_code, const String &p_message) {
	Dictionary d;
	d["ok"] = false;
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	d["error"] = error;
	return d;
}

// 场景路径必须是当前编辑场景内的相对路径：拒绝绝对路径、父级穿越（..）与
// subname（"Player:garbage" 会静默解析到 Player——禁止，保证精确目标语义）。
static bool _is_valid_scene_path(const String &p_path) {
	if (p_path.is_empty()) {
		return false;
	}
	const NodePath np(p_path);
	if (np.is_absolute() || np.get_subname_count() != 0) {
		return false;
	}
	for (const StringName &n : np.get_names()) {
		if (n == "..") {
			return false;
		}
	}
	return true;
}

// 从 JSON 数组元素取数值（JSON 数字为 INT 或 FLOAT）；拒绝非有限值（Inf/NaN）。
static bool _arr_num(const Array &p_arr, int p_idx, double &r_val) {
	if (p_idx < 0 || p_idx >= p_arr.size()) {
		return false;
	}
	const Variant &v = p_arr[p_idx];
	if (v.get_type() == Variant::INT) {
		r_val = (double)(int64_t)v;
		return true;
	}
	if (v.get_type() == Variant::FLOAT) {
		const double d = (double)v;
		if (!Math::is_finite(d)) {
			return false;
		}
		r_val = d;
		return true;
	}
	return false;
}

// 取 int32 组件（Vector*i 等）：拒绝非有限、非整数值与越界（int32 范围）。
static bool _arr_int32(const Array &p_arr, int p_idx, int32_t &r_val) {
	double d = 0;
	if (!_arr_num(p_arr, p_idx, d)) {
		return false;
	}
	if (d != Math::floor(d) || d < -2147483648.0 || d > 2147483647.0) {
		return false;
	}
	r_val = (int32_t)d;
	return true;
}

// 简单类型互转（本 fork 无 Variant::convert，手工实现；失败返回 false）。
static bool _simple_convert(Variant::Type p_type, const Variant &p_value, Variant &r_out) {
	switch (p_type) {
		case Variant::INT:
			if (p_value.get_type() == Variant::FLOAT) {
				const double d = (double)p_value;
				// 拒绝非有限/非整数值与 int64 越界（防 UB 与静默截断）。
				if (!Math::is_finite(d) || d != Math::floor(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) {
					return false;
				}
				r_out = (int64_t)d;
				return true;
			}
			if (p_value.get_type() == Variant::BOOL) {
				r_out = (int64_t)((bool)p_value ? 1 : 0);
				return true;
			}
			return false;
		case Variant::FLOAT:
			if (p_value.get_type() == Variant::INT) {
				r_out = (double)p_value;
				return true;
			}
			return false;
		case Variant::BOOL:
			if (p_value.get_type() == Variant::INT) {
				r_out = (int64_t)p_value != 0;
				return true;
			}
			return false;
		case Variant::STRING:
			if (p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT || p_value.get_type() == Variant::BOOL) {
				r_out = (String)p_value;
				return true;
			}
			return false;
		case Variant::STRING_NAME:
			if (p_value.get_type() == Variant::STRING) {
				r_out = StringName((String)p_value);
				return true;
			}
			return false;
		case Variant::NODE_PATH:
			if (p_value.get_type() == Variant::STRING) {
				r_out = NodePath((String)p_value);
				return true;
			}
			return false;
		default:
			return false;
	}
}

// JSON 值 → 目标属性类型转换。转换失败返回 false（r_err 说明原因），
// 绝不静默写入错误值（如 [1,2,3] 变 (0,0,0)）。
static bool _convert_prop_value(Variant::Type p_type, const Variant &p_value, Variant &r_out, String &r_err) {
	if (p_type == Variant::NIL) {
		r_out = p_value; // 无类型信息（如脚本动态属性）：原样接受，由 setter 决定
		return true;
	}
	if (p_value.get_type() == p_type) {
		// 同类型直通：标量 FLOAT 需拒绝非有限值（JSON 1e99999 可解析为 Inf）。
		if (p_type == Variant::FLOAT && !Math::is_finite((double)p_value)) {
			r_err = "非有限数值（Inf/NaN）";
			return false;
		}
		r_out = p_value;
		return true;
	}
	// 数学类型：JSON 数组 → 元素构造（要求精确元素个数，杜绝 [1,2,3] 静默截断成 Vector2）。
	if (p_value.get_type() == Variant::ARRAY) {
		const Array arr = p_value;
		switch (p_type) {
			case Variant::VECTOR2:
			case Variant::VECTOR2I: {
				if (arr.size() != 2) {
					break;
				}
				if (p_type == Variant::VECTOR2I) {
					int32_t x = 0, y = 0;
					if (!_arr_int32(arr, 0, x) || !_arr_int32(arr, 1, y)) {
						break;
					}
					r_out = Vector2i(x, y);
				} else {
					double x = 0, y = 0;
					if (!_arr_num(arr, 0, x) || !_arr_num(arr, 1, y)) {
						break;
					}
					r_out = Vector2(x, y);
				}
				return true;
			}
			case Variant::VECTOR3:
			case Variant::VECTOR3I: {
				if (arr.size() != 3) {
					break;
				}
				if (p_type == Variant::VECTOR3I) {
					int32_t x = 0, y = 0, z = 0;
					if (!_arr_int32(arr, 0, x) || !_arr_int32(arr, 1, y) || !_arr_int32(arr, 2, z)) {
						break;
					}
					r_out = Vector3i(x, y, z);
				} else {
					double x = 0, y = 0, z = 0;
					if (!_arr_num(arr, 0, x) || !_arr_num(arr, 1, y) || !_arr_num(arr, 2, z)) {
						break;
					}
					r_out = Vector3(x, y, z);
				}
				return true;
			}
			case Variant::VECTOR4:
			case Variant::VECTOR4I: {
				if (arr.size() != 4) {
					break;
				}
				if (p_type == Variant::VECTOR4I) {
					int32_t x = 0, y = 0, z = 0, w = 0;
					if (!_arr_int32(arr, 0, x) || !_arr_int32(arr, 1, y) || !_arr_int32(arr, 2, z) || !_arr_int32(arr, 3, w)) {
						break;
					}
					r_out = Vector4i(x, y, z, w);
				} else {
					double x = 0, y = 0, z = 0, w = 0;
					if (!_arr_num(arr, 0, x) || !_arr_num(arr, 1, y) || !_arr_num(arr, 2, z) || !_arr_num(arr, 3, w)) {
						break;
					}
					r_out = Vector4(x, y, z, w);
				}
				return true;
			}
			case Variant::COLOR: {
				double r = 0, g = 0, b = 0, a = 1;
				if (arr.size() == 3) {
					if (!_arr_num(arr, 0, r) || !_arr_num(arr, 1, g) || !_arr_num(arr, 2, b)) {
						break;
					}
				} else if (arr.size() == 4) {
					if (!_arr_num(arr, 0, r) || !_arr_num(arr, 1, g) || !_arr_num(arr, 2, b) || !_arr_num(arr, 3, a)) {
						break;
					}
				} else {
					break;
				}
				r_out = Color(r, g, b, a);
				return true;
			}
			case Variant::RECT2:
			case Variant::RECT2I: {
				// 支持平铺 [x,y,w,h] 与嵌套 [[x,y],[w,h]]。
				Variant pos, size;
				String e;
				if (arr.size() == 4) {
					if (p_type == Variant::RECT2I) {
						int32_t x = 0, y = 0, w = 0, h = 0;
						if (!_arr_int32(arr, 0, x) || !_arr_int32(arr, 1, y) || !_arr_int32(arr, 2, w) || !_arr_int32(arr, 3, h)) {
							break;
						}
						r_out = Rect2i(x, y, w, h);
					} else {
						double x = 0, y = 0, w = 0, h = 0;
						if (!_arr_num(arr, 0, x) || !_arr_num(arr, 1, y) || !_arr_num(arr, 2, w) || !_arr_num(arr, 3, h)) {
							break;
						}
						r_out = Rect2(x, y, w, h);
					}
					return true;
				}
				if (arr.size() == 2 && _convert_prop_value(p_type == Variant::RECT2I ? Variant::VECTOR2I : Variant::VECTOR2, arr[0], pos, e) && _convert_prop_value(p_type == Variant::RECT2I ? Variant::VECTOR2I : Variant::VECTOR2, arr[1], size, e)) {
					r_out = (p_type == Variant::RECT2I) ? Variant(Rect2i((Vector2i)pos, (Vector2i)size)) : Variant(Rect2((Vector2)pos, (Vector2)size));
					return true;
				}
				break;
			}
			case Variant::QUATERNION: {
				if (arr.size() != 4) {
					break;
				}
				double x = 0, y = 0, z = 0, w = 0;
				if (!_arr_num(arr, 0, x) || !_arr_num(arr, 1, y) || !_arr_num(arr, 2, z) || !_arr_num(arr, 3, w)) {
					break;
				}
				r_out = Quaternion(x, y, z, w);
				return true;
			}
			case Variant::PLANE: {
				if (arr.size() != 4) {
					break;
				}
				double x = 0, y = 0, z = 0, d = 0;
				if (!_arr_num(arr, 0, x) || !_arr_num(arr, 1, y) || !_arr_num(arr, 2, z) || !_arr_num(arr, 3, d)) {
					break;
				}
				r_out = Plane(x, y, z, d);
				return true;
			}
			case Variant::BASIS: {
				Variant x_axis, y_axis, z_axis;
				String e;
				if (arr.size() == 3 && _convert_prop_value(Variant::VECTOR3, arr[0], x_axis, e) && _convert_prop_value(Variant::VECTOR3, arr[1], y_axis, e) && _convert_prop_value(Variant::VECTOR3, arr[2], z_axis, e)) {
					r_out = Basis((Vector3)x_axis, (Vector3)y_axis, (Vector3)z_axis); // 显式转换避开重载歧义
					return true;
				}
				break;
			}
			case Variant::TRANSFORM2D: {
				Variant x_axis, y_axis, origin;
				String e;
				if (arr.size() == 3 && _convert_prop_value(Variant::VECTOR2, arr[0], x_axis, e) && _convert_prop_value(Variant::VECTOR2, arr[1], y_axis, e) && _convert_prop_value(Variant::VECTOR2, arr[2], origin, e)) {
					r_out = Transform2D((Vector2)x_axis, (Vector2)y_axis, (Vector2)origin);
					return true;
				}
				break;
			}
			case Variant::TRANSFORM3D: {
				Variant basis, origin;
				String e;
				if (arr.size() == 2 && _convert_prop_value(Variant::BASIS, arr[0], basis, e) && _convert_prop_value(Variant::VECTOR3, arr[1], origin, e)) {
					r_out = Transform3D((Basis)basis, (Vector3)origin);
					return true;
				}
				break;
			}
			case Variant::AABB: {
				Variant pos, size;
				String e;
				if (arr.size() == 2 && _convert_prop_value(Variant::VECTOR3, arr[0], pos, e) && _convert_prop_value(Variant::VECTOR3, arr[1], size, e)) {
					r_out = AABB((Vector3)pos, (Vector3)size);
					return true;
				}
				break;
			}
			default:
				break;
		}
	}
	// 简单类型互转（手工实现，不静默吞错）。
	if (_simple_convert(p_type, p_value, r_out)) {
		return true;
	}
	r_err = "无法将 " + Variant::get_type_name(p_value.get_type()) + " 转换为 " + Variant::get_type_name(p_type);
	return false;
}

Dictionary SemanticOps::activate(const String &p_id) {
	// TreeItem 语义目标："<树控件>/item"（根 item，叶子根场景节点也在此）或
	// "<树控件>/item/<索引>:<文本>/…"。取最后一个 "/item" 段尝试；前缀必须能解析
	// 为 Tree 控件，否则整体回退为普通控件激活（防误分类含 "item" 段的控件 ID）。
	int item_sep = p_id.rfind("/item");
	while (item_sep >= 0) {
		const bool terminal = p_id.length() == item_sep + 5;
		const bool continued = p_id.length() > item_sep + 5 && p_id[item_sep + 5] == '/';
		if (terminal || continued) {
			break;
		}
		if (item_sep == 0) {
			item_sep = -1;
			break;
		}
		item_sep = p_id.rfind("/item", item_sep - 1);
	}
	if (item_sep >= 0) {
		const String ctrl_id = p_id.substr(0, item_sep);
		const String item_path = p_id.substr(item_sep);
		Control *ctrl = EditorUiTree::find_control(ctrl_id);
		Tree *tree = Object::cast_to<Tree>(ctrl);
		if (tree) {
			TreeItem *item = EditorUiTree::find_tree_item(tree, item_path);
			if (!item) {
				return _err("item_not_found", "Tree 中未找到 item: " + item_path);
			}
			tree->scroll_to_item(item);
			tree->set_selected(item); // 触发 item_selected（SceneTreeDock 选中链路）
			return _ok(Dictionary());
		}
		// 前缀不是 Tree：整体按控件 ID 处理。
	}
	Control *ctrl = EditorUiTree::find_control(p_id);
	if (!ctrl) {
		return _err("control_not_found", "语义 ID 未找到控件: " + p_id);
	}
	if (!ctrl->is_visible_in_tree()) {
		return _err("not_visible", "控件不可见，无法激活: " + p_id);
	}
	// Button 族（含直接继承 BaseButton 的 LinkButton/TextureButton）：走真实输入路径
	// （BaseButton::gui_input 的 ui_accept 分支——与无障碍激活同路），完整覆盖
	// disabled 检查、toggle/ButtonGroup、弹层打开；不手工 emit pressed（绕过语义）。
	if (BaseButton *bb = Object::cast_to<BaseButton>(ctrl)) {
		if (bb->is_disabled()) {
			return _err("disabled", "控件已禁用: " + p_id);
		}
		Ref<InputEventAction> press;
		press.instantiate();
		press->set_action("ui_accept");
		press->set_pressed(true);
		ctrl->gui_input(press);
		Ref<InputEventAction> release;
		release.instantiate();
		release->set_action("ui_accept");
		release->set_pressed(false);
		ctrl->gui_input(release);
		return _ok(Dictionary());
	}
	// 通用回退：InputEvent 投递（坐标由引擎算）。
	return _activate_input_fallback(ctrl);
}

Dictionary SemanticOps::set_text(const String &p_id, const String &p_value) {
	Control *ctrl = EditorUiTree::find_control(p_id);
	if (!ctrl) {
		return _err("control_not_found", "语义 ID 未找到控件: " + p_id);
	}
	if (LineEdit *le = Object::cast_to<LineEdit>(ctrl)) {
		if (!le->is_editable()) {
			return _err("not_editable", "控件只读: " + p_id);
		}
		le->set_text(p_value);
		// 程序化 set_text 不发 text_changed（本 fork），手动补发；但必须发存储后的值
		// （max_length 可能截断）而非请求值。
		const String stored = le->get_text();
		le->emit_signal(SceneStringName(text_changed), stored);
		Dictionary result;
		result["applied"] = stored;
		return _ok(result);
	}
	if (TextEdit *te = Object::cast_to<TextEdit>(ctrl)) {
		if (!te->is_editable()) {
			return _err("not_editable", "控件只读: " + p_id);
		}
		te->set_text(p_value);
		te->emit_signal(SceneStringName(text_changed));
		Dictionary result;
		result["applied"] = te->get_text();
		return _ok(result);
	}
	if (SpinBox *sb = Object::cast_to<SpinBox>(ctrl)) {
		if (!sb->is_editable()) {
			return _err("not_editable", "控件只读: " + p_id);
		}
		if (!p_value.is_valid_float()) {
			return _err("invalid_value", "非法数值: '" + p_value + "'");
		}
		const double v = p_value.to_float();
		if (!Math::is_finite(v)) {
			return _err("invalid_value", "非法数值: '" + p_value + "'");
		}
		sb->set_value(v); // Range::set_value 自带 clamp 且只发一次 value_changed
		Dictionary result;
		result["applied"] = sb->get_value();
		return _ok(result);
	}
	return _err("unsupported_role", "控件不支持 set_text: " + String(ctrl->get_class()));
}

Dictionary SemanticOps::focus(const String &p_id) {
	Control *ctrl = EditorUiTree::find_control(p_id);
	if (!ctrl) {
		return _err("control_not_found", "语义 ID 未找到控件: " + p_id);
	}
	ctrl->grab_focus();
	return _ok(Dictionary());
}

Dictionary SemanticOps::get_ui_tree() {
	return _ok(EditorUiTree::export_tree());
}

Dictionary SemanticOps::select_node(const String &p_path) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	if (!_is_valid_scene_path(p_path)) {
		return _err("invalid_path", "路径必须为场景内相对路径（禁止绝对路径/..）: " + p_path);
	}
	Node *target = root->get_node_or_null(NodePath(p_path));
	if (!target) {
		return _err("node_not_found", "场景中未找到节点: " + p_path);
	}
	if (target != root && !root->is_ancestor_of(target)) {
		return _err("invalid_path", "目标不在当前编辑场景内: " + p_path);
	}
	// 替换选择（与人工点击一致）：清空 → 选中 → update（触发 selection_changed 全链路）。
	EditorSelection *sel = EditorNode::get_singleton()->get_editor_selection();
	sel->clear();
	sel->add_node(target);
	sel->update();
	return _ok(String(target->get_path()));
}

Dictionary SemanticOps::set_prop(const String &p_path, const String &p_prop, const Variant &p_value) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	if (!_is_valid_scene_path(p_path)) {
		return _err("invalid_path", "路径必须为场景内相对路径（禁止绝对路径/..）: " + p_path);
	}
	Node *target = root->get_node_or_null(NodePath(p_path));
	if (!target) {
		return _err("node_not_found", "场景中未找到节点: " + p_path);
	}
	if (target != root && !root->is_ancestor_of(target)) {
		return _err("invalid_path", "目标不在当前编辑场景内: " + p_path);
	}
	// 属性存在性 + 类型信息：从对象属性列表解析（统一覆盖原生与脚本属性）。
	PropertyInfo pi;
	bool found = false;
	{
		List<PropertyInfo> list;
		target->get_property_list(&list);
		for (const PropertyInfo &e : list) {
			if (e.name == StringName(p_prop)) {
				pi = e;
				found = true;
				break;
			}
		}
	}
	if (!found) {
		return _err("prop_not_found", "节点无此属性: " + p_prop);
	}
	// 属性策略：只允许 Inspector 可见属性（与人工一致）；拒绝只读/内部/无编辑器面属性，
	// 防止意外改坏序列化/运行时内部状态（如 scene_file_path、unique_name_in_owner）。
	// 注意：PROPERTY_USAGE_NO_EDITOR 就是 STORAGE（bit0），不能按位与判断——
	// 正确语义是“要求 EDITOR 位”。
	if ((pi.usage & PROPERTY_USAGE_READ_ONLY) || (pi.usage & PROPERTY_USAGE_INTERNAL) || !(pi.usage & PROPERTY_USAGE_EDITOR)) {
		return _err("read_only", "属性不可经 AI 修改（只读/内部/非编辑器属性）: " + p_prop);
	}
	// JSON 值 → 目标属性类型转换（校验失败即报错，不写入）。
	Variant value;
	String conv_err;
	if (!_convert_prop_value(pi.type, p_value, value, conv_err)) {
		return _err("invalid_params", "属性 '" + p_prop + "' 的值非法: " + conv_err);
	}
	// hint 校验：enum/flags 取值范围（防非法枚举值被 setter 静默吞掉仍报 ok）。
	if (pi.hint == PROPERTY_HINT_ENUM || pi.hint == PROPERTY_HINT_FLAGS) {
		const int count = pi.hint_string.split(",").size();
		const int64_t v = (int64_t)value;
		if (pi.hint == PROPERTY_HINT_ENUM && (v < 0 || v >= count)) {
			return _err("invalid_params", "属性 '" + p_prop + "' 的枚举值越界: " + itos(v));
		}
		if (pi.hint == PROPERTY_HINT_FLAGS && (v < 0 || v >= (int64_t(1) << count))) {
			return _err("invalid_params", "属性 '" + p_prop + "' 的 flags 值越界: " + itos(v));
		}
	}
	// 当前值（undo 基线）；get 失败视为属性不可读。
	bool valid = false;
	const Variant old_val = target->get(StringName(p_prop), &valid);
	if (!valid) {
		return _err("prop_not_found", "节点属性不可读: " + p_prop);
	}
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("AI Set Prop: " + p_prop);
	eurm->add_do_property(target, StringName(p_prop), value);
	if (Control *ctrl = Object::cast_to<Control>(target)) {
		if (p_prop == "anchors_preset" || p_prop == "layout_mode") {
			// 与 Inspector 一致的联动状态撤销（editor_inspector.cpp:_edit_set：
			// 布局类属性需整状态快照，单属性 undo 恢复不全）。
			eurm->add_undo_method(ctrl, "_edit_set_state", ctrl->_edit_get_state());
		} else {
			eurm->add_undo_property(target, StringName(p_prop), old_val);
		}
	} else {
		eurm->add_undo_property(target, StringName(p_prop), old_val);
	}
	// 联动属性撤销（与 Inspector 同栈）：静态（ClassDB 注册）+ 动态（脚本回调）。
	List<StringName> linked;
	ClassDB::get_linked_properties_info(target->get_class_name(), StringName(p_prop), &linked);
	for (const StringName &lp : linked) {
		bool lv = false;
		const Variant uv = target->get(lp, &lv);
		if (lv) {
			eurm->add_undo_property(target, lp, uv);
		}
	}
	if (target->has_method("_get_linked_undo_properties")) {
		const Variant dyn = target->call("_get_linked_undo_properties", StringName(p_prop), value);
		if (dyn.get_type() == Variant::PACKED_STRING_ARRAY) {
			for (const String &dp : dyn.operator PackedStringArray()) {
				bool lv = false;
				const Variant uv = target->get(StringName(dp), &lv);
				if (lv) {
					eurm->add_undo_property(target, StringName(dp), uv);
				}
			}
		}
	}
	eurm->commit_action();
	return _ok(Dictionary());
}

Dictionary SemanticOps::get_state() {
	Dictionary result;
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	result["has_scene"] = root != nullptr;
	if (root) {
		result["scene_root"] = String(root->get_path());
	}
	Array selection;
	if (root) {
		List<Node *> nodes = EditorNode::get_singleton()->get_editor_selection()->get_full_selected_node_list();
		for (Node *n : nodes) {
			selection.append(String(root->get_path_to(n)));
		}
	}
	result["selection"] = selection;
	result["can_undo"] = EditorUndoRedoManager::get_singleton()->has_undo();
	result["can_redo"] = EditorUndoRedoManager::get_singleton()->has_redo();
	return _ok(result);
}

Dictionary SemanticOps::undo() {
	const bool ok = EditorUndoRedoManager::get_singleton()->undo();
	if (!ok) {
		return _err("nothing_to_undo", "没有可撤销的操作");
	}
	return _ok(Dictionary());
}

Dictionary SemanticOps::redo() {
	const bool ok = EditorUndoRedoManager::get_singleton()->redo();
	if (!ok) {
		return _err("nothing_to_redo", "没有可重做的操作");
	}
	return _ok(Dictionary());
}

Dictionary SemanticOps::get_node_count() {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	// 递归统计（含根自身）。
	int count = 0;
	List<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty()) {
		Node *n = stack.back()->get();
		stack.pop_back();
		count++;
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
	return _ok(count);
}

Dictionary SemanticOps::create_node(const String &p_name) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	String name = p_name.strip_edges();
	if (name.is_empty()) {
		name = "AINode";
	}
	Node3D *node = memnew(Node3D);
	node->set_name(name); // 非法字符会被净化；重名 add_child 自动加后缀——返回最终结果
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("AI Create Node");
	eurm->add_do_method(root, "add_child", node, true);
	eurm->add_undo_method(root, "remove_child", node);
	eurm->add_do_method(node, "set_owner", root);
	eurm->add_do_reference(node);
	eurm->commit_action();
	// 返回最终路径/名称（set_name 净化 + add_child 重名后缀后可能不同于请求值）。
	Dictionary result;
	result["instance_id"] = (uint64_t)node->get_instance_id();
	result["path"] = String(root->get_path_to(node));
	result["name"] = String(node->get_name());
	return _ok(result);
}

Dictionary SemanticOps::_activate_input_fallback(Control *p_ctrl) {
	if (!p_ctrl->is_visible_in_tree()) {
		return _err("not_visible", "控件不可见，无法激活: " + p_ctrl->get_name());
	}
	Viewport *vp = p_ctrl->get_viewport();
	if (!vp) {
		return _err("no_viewport", "控件无视口");
	}
	// 坐标由引擎从布局计算（Control 全局 rect 中心）——AI 不猜坐标。
	const Point2 center = p_ctrl->get_global_rect().get_center();
	Ref<InputEventMouseButton> ev;
	ev.instantiate();
	ev->set_position(center);
	ev->set_button_index(MouseButton::LEFT);
	ev->set_pressed(true);
	vp->push_input(ev);
	Ref<InputEventMouseButton> up;
	up.instantiate();
	up->set_position(center);
	up->set_button_index(MouseButton::LEFT);
	up->set_pressed(false);
	vp->push_input(up);
	return _ok(Dictionary());
}

#endif // TOOLS_ENABLED
