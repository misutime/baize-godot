/**************************************************************************/
/*  registry.cpp                                                 */
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

#include "registry.h"

#ifdef TOOLS_ENABLED

#include "ops.h"

Vector<Registry::Method> Registry::s_methods;
bool Registry::s_registered = false;

// 错误构造（与 ops.cpp 同款；handler 使用于下方，定义须在 handler 之前）。
static Dictionary _err(const String &p_code, const String &p_message) {
	Dictionary d;
	d["ok"] = false;
	Dictionary e;
	e["code"] = p_code;
	e["message"] = p_message;
	d["error"] = e;
	return d;
}

// ---- 参数解包 handler（Ops 适配层）----

static Dictionary _h_ui_get_tree(const Dictionary &p_args) {
	return Ops::get_ui_tree();
}

static Dictionary _h_ui_activate(const Dictionary &p_args) {
	return Ops::activate(p_args.get("id", "").operator String());
}

static Dictionary _h_ui_set_text(const Dictionary &p_args) {
	return Ops::set_text(p_args.get("id", "").operator String(), p_args.get("value", "").operator String());
}

static Dictionary _h_ui_focus(const Dictionary &p_args) {
	return Ops::focus(p_args.get("id", "").operator String());
}

static Dictionary _h_editor_select_node(const Dictionary &p_args) {
	return Ops::select_node(p_args.get("path", "").operator String());
}

static Dictionary _h_editor_set_prop(const Dictionary &p_args) {
	return Ops::set_prop(p_args.get("path", "").operator String(), p_args.get("prop", "").operator String(), p_args.get("value", Variant()));
}

static Dictionary _h_editor_get_state(const Dictionary &p_args) {
	return Ops::get_state();
}

static Dictionary _h_editor_undo(const Dictionary &p_args) {
	return Ops::undo();
}

static Dictionary _h_editor_redo(const Dictionary &p_args) {
	return Ops::redo();
}

static Dictionary _h_scene_get_node_count(const Dictionary &p_args) {
	return Ops::get_node_count();
}

static Dictionary _h_scene_create_node(const Dictionary &p_args) {
	return Ops::create_node(p_args.get("name", "").operator String());
}

static Dictionary _h_scene_get_node_position(const Dictionary &p_args) {
	// 评审 P2：node_path 必须为字符串（validate_args 只查 required 存在性，不查类型——
	// 隐式 operator String 会把数字 123 转 "123"，可能命中名为 123 的节点）。
	const Variant path_var = p_args.get("node_path", Variant());
	if (path_var.get_type() != Variant::STRING) {
		return _err("invalid_params", "node_path 必须为字符串");
	}
	return Ops::get_node_position(path_var.operator String());
}

static Dictionary _h_scene_set_node_position(const Dictionary &p_args) {
	const Variant path_var = p_args.get("node_path", Variant());
	if (path_var.get_type() != Variant::STRING) {
		return _err("invalid_params", "node_path 必须为字符串");
	}
	return Ops::set_node_position(
			path_var.operator String(),
			p_args.get("position", Dictionary()).operator Dictionary());
}

static Dictionary _h_editor_get_ui_font_size(const Dictionary &p_args) {
	return Ops::get_ui_font_size();
}

static Dictionary _h_editor_get_ui_scale(const Dictionary &p_args) {
	return Ops::get_ui_scale();
}

static Dictionary _h_editor_get_ui_font(const Dictionary &p_args) {
	return Ops::get_ui_font();
}

static Dictionary _h_editor_get_ui_font_bold(const Dictionary &p_args) {
	return Ops::get_ui_font_bold();
}

// ---- 工具元数据 ----

static Dictionary _schema(const Dictionary &p_props, const Vector<String> &p_required) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_props;
	if (!p_required.is_empty()) {
		Array req;
		for (const String &r : p_required) {
			req.append(r);
		}
		schema["required"] = req;
	}
	return schema;
}

static Dictionary _str_param(const String &p_desc) {
	Dictionary d;
	d["type"] = "string";
	d["description"] = p_desc;
	return d;
}

static Dictionary _num_param(const String &p_desc) {
	Dictionary d;
	d["type"] = "number";
	d["description"] = p_desc;
	return d;
}

void Registry::register_method(const String &p_name, const String &p_desc, const Dictionary &p_schema, Handler p_handler) {
	Method m;
	m.name = p_name;
	m.description = p_desc;
	m.input_schema = p_schema;
	m.handler = p_handler;
	s_methods.push_back(m);
}

void Registry::_register_all() {
	const Dictionary id_param = _str_param("语义 ID（ui.get_tree 返回的 id 字段；TreeItem 用 '<树控件 id>/item/<索引>…'）");
	register_method("ui.get_tree", "导出编辑器 UI 语义树（role/name/state/items，含场景树 TreeItem）", _schema({}, {}), _h_ui_get_tree);
	register_method("ui.activate", "激活语义目标（Button→真实输入路径等效点击；TreeItem→选中）", _schema({ { "id", id_param } }, { "id" }), _h_ui_activate);
	const Dictionary value_param = _str_param("文本/数值内容");
	register_method("ui.set_text", "文本/数值输入（LineEdit/TextEdit/SpinBox；只读控件拒绝）", _schema({ { "id", id_param }, { "value", value_param } }, { "id", "value" }), _h_ui_set_text);
	register_method("ui.focus", "聚焦控件", _schema({ { "id", id_param } }, { "id" }), _h_ui_focus);

	const Dictionary path_param = _str_param("场景相对路径（\".\" = 根；禁止绝对路径/..）");
	register_method("editor.select_node", "选中场景节点（与人工点击一致，走 EditorSelection）", _schema({ { "path", path_param } }, { "path" }), _h_editor_select_node);
	Dictionary prop_param = _str_param("属性名");
	Dictionary any_value_param;
	any_value_param["description"] = "任意 JSON 值（按目标属性类型转换：数组→Vector/Color/Transform 等）";
	register_method("editor.set_prop", "设置场景节点属性（undo 入栈，与 Inspector 联动撤销一致）", _schema({ { "path", path_param }, { "prop", prop_param }, { "value", any_value_param } }, { "path", "prop", "value" }), _h_editor_set_prop);
	register_method("editor.get_state", "编辑器状态（场景/选中/undo 栈）", _schema({}, {}), _h_editor_get_state);
	register_method("editor.undo", "撤销上一步", _schema({}, {}), _h_editor_undo);
	register_method("editor.redo", "重做上一步", _schema({}, {}), _h_editor_redo);

	register_method("scene.get_node_count", "场景节点数（含根）", _schema({}, {}), _h_scene_get_node_count);
	const Dictionary name_param = _str_param("新节点名（非法字符被净化；返回最终 path）");
	register_method("scene.create_node", "创建 Node3D 子节点（undo 可撤销），返回 { instance_id, path, name }", _schema({ { "name", name_param } }, { "name" }), _h_scene_create_node);

	// 能力合流（2026-08-05，迁移自 WebBridge）：场景位置读写 + 编辑器 UI 主题状态（读）。
	const Dictionary node_path_param = _str_param("场景相对路径（\".\" = 根；禁止绝对路径/..）");
	register_method("scene.get_node_position", "读取 Node3D 位置 {x,y,z}", _schema({ { "node_path", node_path_param } }, { "node_path" }), _h_scene_get_node_position);
	// position 为 {x,y,z} 对象（评审 P2：注册 object schema 而非字符串，避免 schema 驱动调用者生成错误载荷）。
	Dictionary position_props;
	position_props["x"] = _num_param("X 坐标（有限数字）");
	position_props["y"] = _num_param("Y 坐标（有限数字）");
	position_props["z"] = _num_param("Z 坐标（有限数字）");
	const Dictionary position_param = _schema(position_props, { "x", "y", "z" });
	register_method("scene.set_node_position", "设置 Node3D 位置（undo 入栈，与人工一致）", _schema({ { "node_path", node_path_param }, { "position", position_param } }, { "node_path", "position" }), _h_scene_set_node_position);
	register_method("editor.get_ui_font_size", "编辑器主字体大小（main_font_size，默认 14）", _schema({}, {}), _h_editor_get_ui_font_size);
	register_method("editor.get_ui_scale", "编辑器界面生效缩放（display_scale 实际值）", _schema({}, {}), _h_editor_get_ui_scale);
	register_method("editor.get_ui_font", "编辑器实际生效主字体路径（main_font → 默认解析路径）", _schema({}, {}), _h_editor_get_ui_font);
	register_method("editor.get_ui_font_bold", "编辑器实际生效粗体字体路径（main_font_bold → main_font → 默认解析路径）", _schema({}, {}), _h_editor_get_ui_font_bold);
}

void Registry::ensure_registered() {
	if (!s_registered) {
		_register_all();
		s_registered = true;
	}
}

const Registry::Method *Registry::find(const String &p_name) {
	ensure_registered();
	for (const Method &m : s_methods) {
		if (m.name == p_name) {
			return &m;
		}
	}
	return nullptr;
}

const Vector<Registry::Method> &Registry::methods() {
	ensure_registered();
	return s_methods;
}

bool Registry::validate_args(const Method &p_method, const Variant &p_params, Dictionary &r_args, String &r_err) {
	const Dictionary schema = p_method.input_schema;
	const bool has_required = schema.has("required");
	if (p_params.get_type() != Variant::DICTIONARY) {
		// 无必需参数的方法允许省略 params（视为空对象）；有必需参数的必须传对象。
		if (p_params.get_type() == Variant::NIL && !has_required) {
			r_args = Dictionary();
			return true;
		}
		r_err = "参数必须是对象: " + p_method.name;
		return false;
	}
	r_args = p_params.operator Dictionary();
	if (!has_required) {
		return true;
	}
	const Array required = schema["required"];
	for (int i = 0; i < required.size(); i++) {
		const String key = required[i].operator String();
		if (!r_args.has(key)) {
			r_err = "缺少必需参数: " + key + "（" + p_method.name + "）";
			return false;
		}
	}
	return true;
}

#endif // TOOLS_ENABLED
