// SPDX-License-Identifier: MIT
#include "registry.h"

#include "core/string/print_string.h"

#include "ops.h"

#ifdef TOOLS_ENABLED

Vector<Registry::Method> Registry::s_methods;
bool Registry::s_registered = false;

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
	// MVP 能力面（2026-08-06）：选中节点位置 XYZ 读写。
	// 完整能力域（project.*/resource.*/run.*/viewport.*）随里程碑扩展。
	register_method("editor.get_state", "编辑器状态（场景/选中/undo 栈）", _schema({}, {}), Ops::h_get_state);

	const Dictionary node_path_param = _str_param("场景相对路径（\".\" = 根；禁止绝对路径/..）");
	register_method("editor.select_node", "选中场景节点（与人工点击一致，走 EditorSelection）", _schema({ { "node_path", node_path_param } }, { "node_path" }), Ops::h_select_node);
	register_method("editor.undo", "撤销上一步", _schema({}, {}), Ops::h_undo);
	register_method("editor.redo", "重做上一步", _schema({}, {}), Ops::h_redo);
	register_method("scene.get_node_position", "读取 Node3D 位置 {x,y,z}", _schema({ { "node_path", node_path_param } }, { "node_path" }), Ops::h_get_node_position);

	Dictionary position_props;
	position_props["x"] = _num_param("X 坐标（有限数字）");
	position_props["y"] = _num_param("Y 坐标（有限数字）");
	position_props["z"] = _num_param("Z 坐标（有限数字）");
	const Dictionary position_param = _schema(position_props, { "x", "y", "z" });
	register_method("scene.set_node_position", "设置 Node3D 位置（undo 入栈，与人工一致）", _schema({ { "node_path", node_path_param }, { "position", position_param } }, { "node_path", "position" }), Ops::h_set_node_position);

	// —— M1 场景能力面：树/属性读写/增删节点/保存（2026-08-06） ——
	register_method("scene.get_tree", "读取编辑场景树（TreeNode 递归结构；无打开场景 → null）", _schema({}, {}), Ops::h_get_tree);

	Dictionary prop_param = _str_param("属性名（get_props 返回的 name）");
	Dictionary value_param;
	value_param["description"] = "属性值（任意 JSON；按属性类型严格解码，非法结构拒绝）";
	register_method("scene.get_props", "读取节点属性列表（PropInfo：name/type/editable/value）", _schema({ { "node_path", node_path_param } }, { "node_path" }), Ops::h_get_props);
	register_method("scene.set_prop", "设置节点属性（undo 入栈；值按类型解码，INT 可作 FLOAT 源）", _schema({ { "node_path", node_path_param }, { "prop", prop_param }, { "value", value_param } }, { "node_path", "prop", "value" }), Ops::h_set_prop);

	Dictionary type_param = _str_param("Godot 类名（如 Node3D/Node2D；必须可实例化且继承 Node）");
	Dictionary name_param = _str_param("节点名（缺省用类名；自动保证唯一）");
	Dictionary parent_param = _str_param("父节点场景相对路径（缺省 = 场景根 \".\"）");
	register_method("scene.create_node", "在场景中创建节点（undo 入栈，与人工创建一致）", _schema({ { "type", type_param }, { "name", name_param }, { "parent_path", parent_param } }, { "type" }), Ops::h_create_node);
	register_method("scene.remove_node", "从场景删除节点（undo 可恢复，含子树 owner）", _schema({ { "node_path", node_path_param } }, { "node_path" }), Ops::h_remove_node);

	Dictionary path_param = _str_param("保存路径（res:// 或绝对文件系统路径）");
	register_method("editor.save_scene", "保存当前编辑场景（无场景 → no_scene；从未保存过 → not_saved）", _schema({}, {}), Ops::h_save_scene);
	register_method("editor.save_scene_as", "另存当前编辑场景到指定路径", _schema({ { "path", path_param } }, { "path" }), Ops::h_save_scene_as);

	// —— M1 收尾：editor.* 信息面（Electron 皮肤/缩放/标题栏数据源） ——
	register_method("editor.get_theme", "编辑器主题信息（主题名/预设/基础色/强调色/字号）", _schema({}, {}), Ops::h_get_theme);
	register_method("editor.get_scale", "编辑器 UI 缩放比例（EDSCALE）", _schema({}, {}), Ops::h_get_scale);
	register_method("editor.get_project_info", "项目信息（名称/主场景/渲染器/引擎版本/路径）", _schema({}, {}), Ops::h_get_project_info);

	print_line("[gd_provider] Registry 就绪: " + itos(s_methods.size()) + " 个能力方法");
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
