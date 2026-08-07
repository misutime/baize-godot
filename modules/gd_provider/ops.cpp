// SPDX-License-Identifier: MIT
#include "ops.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/property_info.h"
#include "core/templates/pair.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/settings/editor_settings.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/theme.h"
#include "servers/display/display_server.h"

#ifdef TOOLS_ENABLED

// ---- 内部工具（文件内静态） ----

// 文件内错误构造（工具函数在 Ops 类外，无法访问私有 _err）。
static Dictionary _op_err(const String &p_code, const String &p_message) {
	Dictionary d;
	d["ok"] = false;
	Dictionary e;
	e["code"] = p_code;
	e["message"] = p_message;
	d["error"] = e;
	return d;
}

// 保存路径安全：只允许 res:// 项目内路径（禁止 .. 逃逸）或绝对路径（Windows 盘符 / 根斜杠开头），
// 拒绝相对路径——Provider 进程 cwd 下写任意相对路径 = 任意文件写入。
static bool _validate_save_path(const String &p_path, Dictionary &r_err) {
	if (p_path.is_empty()) {
		r_err = _op_err("invalid_params", "path 不能为空");
		return false;
	}
	String rest;
	bool is_res = false;
	if (p_path.begins_with("res://")) {
		is_res = true;
		rest = p_path.substr(6);
	} else if (p_path.length() >= 3 && p_path[1] == ':' && (p_path[2] == '/' || p_path[2] == '\\')) {
		return true; // Windows 盘符绝对路径（X:\ 或 X:/）
	} else if (p_path.begins_with("/") || p_path.begins_with("\\\\")) {
		return true; // POSIX 根 / 或 UNC \\
	} else {
		r_err = _op_err("invalid_params", "保存路径必须是 res:// 项目内路径或绝对路径（拒绝相对路径）: " + p_path);
		return false;
	}
	if (is_res) {
		// res:// 内禁止 .. 逃逸（globalize_path 只做前缀替换，不解析 ..，res://../x 会写出项目）；
		// 反斜杠先归一化为 / 再分段（Windows 分隔符同样能逃逸：res://..\\outside.tscn）
		const Vector<String> segs = rest.replace_char('\\', '/').split("/");
		for (const String &seg : segs) {
			if (seg == "..") {
				r_err = _op_err("invalid_params", "保存路径禁止 .. 逃逸: " + p_path);
				return false;
			}
		}
	}
	return true;
}

// 场景是否处于"未保存"状态（EditorNode 内部标记，保存管线成功才清除）。
static bool _scene_is_unsaved(const String &p_path) {
	const PackedStringArray unsaved = EditorInterface::get_singleton()->get_unsaved_scenes();
	const String local = ProjectSettings::get_singleton()->localize_path(p_path);
	for (const String &u : unsaved) {
		if (u == local || u == p_path) {
			return true;
		}
	}
	return false;
}

// 写盘验证：EditorInterface 保存管线不返回写盘结果（失败只弹编辑器警告，headless 不可见）。
// 判据 = 保存后当前编辑场景不再"未保存"（set_scene_as_saved 仅在 ResourceSaver 成功时执行）；
// 只查 orig（保存前当前场景路径）：save_as 后当前场景路径已变为新路径，目标路径可能是
// 其他标签打开的脏场景（查 target 会误报，review）；save_scene 同路径时 orig == target。
// 不用 mtime/size/md5 对比：内容无变化的合法保存（Save As 当前路径/重复场景）与写盘失败
// 在可观测层不可区分（review 裁定：需引擎保存结果信号，属 M3+ fork 优化；此处接受该漏报）。
static bool _verify_saved(const String &p_path, const String &p_orig_scene_path, Dictionary &r_err) {
	if (!FileAccess::exists(p_path)) {
		r_err = _op_err("save_failed", "保存失败：文件未写入: " + p_path);
		return false;
	}
	const PackedStringArray unsaved = EditorInterface::get_singleton()->get_unsaved_scenes();
	const String orig = ProjectSettings::get_singleton()->localize_path(p_orig_scene_path);
	for (const String &u : unsaved) {
		if (u == orig) {
			r_err = _op_err("save_failed", "保存失败：场景仍标记为未保存（写盘被拒绝或无写入权限）: " + p_path);
			return false;
		}
	}
	// 本来就干净的场景（同路径保存）：保存管线已执行（成功才走 set_scene_as_saved），
	// 内容无变化属合法，接受成功（引擎不暴露写盘结果，见 review 裁定）。
	return true;
}

// 从 Dictionary 取数值成员（INT/FLOAT 均可；p_ints 时仅 INT）。
// 有限性校验：JSON 溢出数字（如 1e400）→ +inf，拒绝写入场景；float 构建下 double 溢出也拒（与 h_set_node_position 一致）。
static bool _num_member(const Dictionary &p_d, const String &p_key, bool p_ints, double &r_val) {
	const Variant v = p_d.get(p_key, Variant());
	if (p_ints) {
		if (v.get_type() != Variant::INT) {
			return false;
		}
		const int64_t i = (int64_t)v;
		// Vector2i/3i/Rect2i 组件是 int32：拒绝 int64 溢出回绕（如 2147483648）
		if (i < INT32_MIN || i > INT32_MAX) {
			return false;
		}
		r_val = (double)i;
		return true;
	}
	if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT) {
		return false;
	}
	r_val = v;
	if (!Math::is_finite(r_val)) {
		return false;
	}
	const real_t r = (real_t)r_val;
	if (!Math::is_finite(r)) {
		return false;
	}
	return true;
}

static bool _num2_from_dict(const Dictionary &p_d, bool p_ints, double &rx, double &ry) {
	return _num_member(p_d, "x", p_ints, rx) && _num_member(p_d, "y", p_ints, ry);
}

static bool _num3_from_dict(const Dictionary &p_d, bool p_ints, double &rx, double &ry, double &rz) {
	return _num_member(p_d, "x", p_ints, rx) && _num_member(p_d, "y", p_ints, ry) && _num_member(p_d, "z", p_ints, rz);
}

// 遍历子树（含自身）快照 owner，供 remove_node 的 undo 恢复。
static void _collect_owner_snapshot(Node *p_node, Vector<Pair<Node *, Variant>> &r_snaps) {
	r_snaps.push_back(Pair<Node *, Variant>(p_node, p_node->get_owner()));
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_owner_snapshot(p_node->get_child(i), r_snaps);
	}
}

Dictionary Ops::_ok(const Variant &p_result) {
	Dictionary d;
	d["ok"] = true;
	d["result"] = p_result;
	return d;
}

Dictionary Ops::_err(const String &p_code, const String &p_message) {
	Dictionary d;
	d["ok"] = false;
	Dictionary e;
	e["code"] = p_code;
	e["message"] = p_message;
	d["error"] = e;
	return d;
}

// ---- Registry handler（薄转发） ----

Dictionary Ops::h_get_state(const Dictionary &p_args) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	Dictionary result;
	result["has_scene"] = root != nullptr;
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

Dictionary Ops::h_select_node(const Dictionary &p_args) {
	// 路径守卫（与 _resolve_node3d 同规则，但接受任意 Node——选中不限于 Node3D）。
	const String p_path = p_args["node_path"];
	if (p_path.begins_with("/") || p_path.begins_with("//")) {
		return _err("invalid_params", "禁止绝对路径: " + p_path);
	}
	const Vector<String> segs = p_path.split("/");
	for (const String &seg : segs) {
		if (seg == "..") {
			return _err("invalid_params", "禁止路径逃逸（..）: " + p_path);
		}
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	Node *target = p_path == "." ? root : root->get_node_or_null(NodePath(p_path));
	if (!target || (target != root && !root->is_ancestor_of(target))) {
		return _err("invalid_node", "找不到节点: " + p_path);
	}
	// 替换选择（与人工点击一致）：清空 → 选中 → update（触发 selection_changed 全链路）。
	EditorSelection *sel = EditorNode::get_singleton()->get_editor_selection();
	sel->clear();
	sel->add_node(target);
	sel->update();
	return _ok(Dictionary());
}

Dictionary Ops::h_undo(const Dictionary &p_args) {
	const bool ok = EditorUndoRedoManager::get_singleton()->undo();
	if (!ok) {
		return _err("nothing_to_undo", "没有可撤销的操作");
	}
	return _ok(Dictionary());
}

Dictionary Ops::h_redo(const Dictionary &p_args) {
	const bool ok = EditorUndoRedoManager::get_singleton()->redo();
	if (!ok) {
		return _err("nothing_to_redo", "没有可重做的操作");
	}
	return _ok(Dictionary());
}

Dictionary Ops::h_get_node_position(const Dictionary &p_args) {
	Dictionary err;
	Node3D *node = _resolve_node3d(p_args["node_path"], err);
	if (!node) {
		return err;
	}
	const Vector3 pos = node->get_position();
	Dictionary result;
	result["x"] = pos.x;
	result["y"] = pos.y;
	result["z"] = pos.z;
	return _ok(result);
}

Dictionary Ops::h_set_node_position(const Dictionary &p_args) {
	// position {x,y,z}：JSON 整数字面量解析为 INT、带小数点为 FLOAT，两者都接受；
	// 有限性校验：JSON 溢出数字（如 1e400）→ +inf，拒绝写入场景。
	const Dictionary p_position = p_args["position"];
	const Variant x = p_position.get("x", Variant());
	const Variant y = p_position.get("y", Variant());
	const Variant z = p_position.get("z", Variant());
	if ((x.get_type() != Variant::FLOAT && x.get_type() != Variant::INT) ||
			(y.get_type() != Variant::FLOAT && y.get_type() != Variant::INT) ||
			(z.get_type() != Variant::FLOAT && z.get_type() != Variant::INT)) {
		return _err("invalid_params", "position 必须为 {x,y,z} 有限数字");
	}
	const double fx = x;
	const double fy = y;
	const double fz = z;
	if (!Math::is_finite(fx) || !Math::is_finite(fy) || !Math::is_finite(fz)) {
		return _err("invalid_params", "position 必须为 {x,y,z} 有限数字");
	}
	// 引擎精度转换可能溢出（float 构建下 1e308 → Inf）：转 real_t 后再校验（review P1；
	// 用 real_t 而非硬编码 float——REAL_T_IS_DOUBLE 构建下 double 坐标不被误拒，review 回归）
	const real_t rx = (real_t)fx;
	const real_t ry = (real_t)fy;
	const real_t rz = (real_t)fz;
	if (!Math::is_finite(rx) || !Math::is_finite(ry) || !Math::is_finite(rz)) {
		return _err("invalid_params", "position 超出引擎精度范围");
	}
	Dictionary err;
	Node3D *node = _resolve_node3d(p_args["node_path"], err);
	if (!node) {
		return err;
	}
	const Vector3 old_pos = node->get_position();
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("Set Position");
	eurm->add_do_method(node, "set_position", Vector3(rx, ry, rz));
	eurm->add_undo_method(node, "set_position", old_pos);
	eurm->commit_action();
	return _ok(Dictionary());
}

Dictionary Ops::h_get_tree(const Dictionary &p_args) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	// 无打开场景 → null（与 scene.changed 事件 payload.tree 语义一致，非错误）；
	// 返回树本身（不包装 {tree:...}）——与 sdk get_tree 类型契约一致。
	return _ok(root ? Variant(serialize_tree(root)) : Variant());
}

Dictionary Ops::h_get_props(const Dictionary &p_args) {
	Dictionary err;
	Node *node = _resolve_node(p_args["node_path"], err);
	if (!node) {
		return err;
	}
	List<PropertyInfo> plist;
	node->get_property_list(&plist);
	Array result;
	for (const PropertyInfo &pi : plist) {
		// 过滤：NIL 类型 / 无 PROPERTY_USAGE_EDITOR / PROPERTY_USAGE_INTERNAL 不对外暴露。
		if (pi.type == Variant::NIL || !(pi.usage & PROPERTY_USAGE_EDITOR) || (pi.usage & PROPERTY_USAGE_INTERNAL)) {
			continue;
		}
		Variant encoded;
		const bool encodable = _encode_value(node->get(pi.name), encoded);
		Dictionary entry;
		entry["name"] = pi.name;
		entry["type"] = Variant::get_type_name(pi.type);
		entry["editable"] = !(pi.usage & PROPERTY_USAGE_READ_ONLY) && encodable;
		entry["value"] = encodable ? encoded : Variant();
		result.append(entry);
	}
	return _ok(result);
}

Dictionary Ops::h_set_prop(const Dictionary &p_args) {
	Dictionary err;
	Node *node = _resolve_node(p_args["node_path"], err);
	if (!node) {
		return err;
	}
	const String prop_name = p_args["prop"].operator String();
	// 在 get_property_list 中找到属性且可编辑（与 get_props 的 editable 判定一致）。
	List<PropertyInfo> plist;
	node->get_property_list(&plist);
	Variant::Type prop_type = Variant::NIL;
	bool editable = false;
	for (const PropertyInfo &pi : plist) {
		if (pi.name == prop_name) {
			prop_type = pi.type;
			editable = (pi.usage & PROPERTY_USAGE_EDITOR) && !(pi.usage & PROPERTY_USAGE_READ_ONLY) && !(pi.usage & PROPERTY_USAGE_INTERNAL);
			break;
		}
	}
	if (prop_type == Variant::NIL || !editable) {
		return _err("invalid_params", "属性不存在或不可编辑: " + prop_name);
	}
	Variant decoded;
	if (!_decode_value(p_args["value"], prop_type, decoded)) {
		return _err("invalid_params", "属性值无法解码为 " + Variant::get_type_name(prop_type) + ": " + prop_name);
	}
	const Variant old_value = node->get(prop_name);
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("Set " + prop_name);
	eurm->add_do_method(node, "set", prop_name, decoded);
	eurm->add_undo_method(node, "set", prop_name, old_value);
	eurm->commit_action();
	return _ok(Dictionary());
}

Dictionary Ops::h_create_node(const Dictionary &p_args) {
	const String type = p_args["type"].operator String();
	// 先验类型：空/未知/非 Node 类直接拒绝（避免 instantiate 出 RefCounted 后再清理）；
	// 抽象类（无 creation_func）→ instantiate 返回 null。
	if (type.is_empty() || !ClassDB::is_parent_class(type, "Node")) {
		return _err("invalid_params", "无法实例化节点类型（空/未知/非 Node 类）: " + type);
	}
	Node *node = Object::cast_to<Node>(ClassDB::instantiate(type));
	if (!node) {
		return _err("invalid_params", "无法实例化节点类型（抽象类）: " + type);
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		memdelete(node);
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	Dictionary err;
	Node *parent = nullptr;
	if (p_args.has("parent_path")) {
		parent = _resolve_node(p_args["parent_path"], err);
		if (!parent) {
			memdelete(node);
			return err;
		}
	} else {
		parent = root;
	}
	// 内部节点守卫：官方创建流程禁止挂到编辑器内部节点下（否则污染编辑器管理树）。
	if (parent->is_internal()) {
		memdelete(node);
		return _err("invalid_params", "不能创建节点到内部节点下: " + p_args["parent_path"].operator String());
	}
	// 命名：显式 name 直接设置；缺省用类名（ClassDB::instantiate 已赋）。一律 validate_child_name 保唯一。
	if (p_args.has("name")) {
		const String name = p_args["name"].operator String();
		if (!name.is_empty()) {
			node->set_name(name);
		}
	}
	// 唯一化：validate_child_name 只返回不设置，需显式 set_name（add_child(_, true) 虽兜底，但显式设置使返回路径与树一致）
	const String unique_name = parent->validate_child_name(node);
	node->set_name(unique_name);

	// undo 模式与 editor/docks/scene_tree_dock.cpp 的人工创建一致（add_child → set_owner → 选中）。
	EditorNode *ed = EditorNode::get_singleton();
	EditorSelection *sel = ed ? ed->get_editor_selection() : nullptr;
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("Create " + type);
	eurm->add_do_method(parent, "add_child", node, true);
	eurm->add_do_method(node, "set_owner", root);
	if (sel) {
		eurm->add_do_method(sel, "clear");
		eurm->add_do_method(sel, "add_node", node);
		// 注意：EditorSelection::update 未绑定 ClassDB，不能经 UndoRedo 字符串调用；
		// 选择信号由 ProviderServer::_sync_selection 在 mutation 信号后 diff 推送（见 provider_server.cpp）。
	}
	eurm->add_do_reference(node);
	eurm->add_undo_method(parent, "remove_child", node);
	eurm->commit_action();

	// commit 已执行 add_child，此刻 get_path_to 有效（根下相对路径，直接子无 "./" 前缀）。
	Dictionary result;
	result["node_path"] = String(root->get_path_to(node));
	return _ok(result);
}

Dictionary Ops::h_remove_node(const Dictionary &p_args) {
	Dictionary err;
	Node *node = _resolve_node(p_args["node_path"], err);
	if (!node) {
		return err;
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (node == root) {
		return _err("invalid_params", "不能删除场景根节点");
	}
	// 内部节点守卫：官方删除路径（SceneTreeDock::_delete_confirm）禁止删除编辑器内部节点。
	if (node->is_internal()) {
		return _err("invalid_params", "不能删除内部节点: " + p_args["node_path"].operator String());
	}
	Node *parent = node->get_parent();
	// 子树（含自身）owner 快照：调用时节点仍挂树，遍历有效；undo 按原 owner 恢复。
	Vector<Pair<Node *, Variant>> owner_snaps;
	_collect_owner_snapshot(node, owner_snaps);
	const int orig_index = node->get_index();

	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("Remove " + String(node->get_name()));
	eurm->add_do_method(parent, "remove_child", node);
	// 不显式 clear 选择：节点被 remove_child 时 EditorSelection::_node_removed 自动移除该节点
	// （仅目标，保留其他选择；无条件 clear 会误清未选中场景的选择，review）；
	// 选择变化信号由 ProviderServer::_sync_selection 在 mutation 后 diff 推送。
	// 引用放 undo 侧（官方删除范式）：do 摘树后由 action 的 do 引用保活没问题，
	// 但 remove→undo 恢复挂树后若用 add_do_reference，新 action 丢弃 redo 分支时会 memdelete 仍挂树的活节点。
	eurm->add_undo_reference(node);
	eurm->add_undo_method(parent, "add_child", node);
	eurm->add_undo_method(parent, "move_child", node, orig_index);
	for (const Pair<Node *, Variant> &snap : owner_snaps) {
		eurm->add_undo_method(snap.first, "set_owner", snap.second);
	}
	eurm->commit_action();
	return _ok(Dictionary());
}

Dictionary Ops::h_save_scene(const Dictionary &p_args) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	const String scene_path = root->get_scene_file_path();
	if (scene_path.is_empty()) {
		return _err("not_saved", "场景从未保存过，没有可用的保存路径");
	}
	// EditorInterface::save_scene 内部只检查根/路径后无条件调 void save_scene_as（写盘失败只弹编辑器警告，
	// headless 下不可见）——用 EditorNode 内部"未保存"状态验证真实写入，失败不得伪造成功。
	const Error err = ei->save_scene();
	if (err != OK) {
		// save_scene 内部对无根/无路径返回 ERR_CANT_CREATE（上面已预判映射，双保险）。
		return _err("no_scene", "保存失败：场景不可用");
	}
	Dictionary verr;
	if (!_verify_saved(scene_path, scene_path, verr)) {
		return verr;
	}
	Dictionary result;
	result["path"] = scene_path;
	return _ok(result);
}

Dictionary Ops::h_save_scene_as(const Dictionary &p_args) {
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		return _err("no_scene", "当前没有打开的编辑场景");
	}
	const String path = p_args["path"].operator String();
	Dictionary perr;
	if (!_validate_save_path(path, perr)) {
		return perr;
	}
	// 与 save_scene 相同的写盘验证（save_scene_as 是 void；判据 = 保存后当前场景不再未保存；
	// 保存前记录原路径——保存后 get_scene_file_path 已变为新路径）。
	const String orig_path = root->get_scene_file_path();
	ei->save_scene_as(path);
	Dictionary verr;
	if (!_verify_saved(path, orig_path, verr)) {
		return verr;
	}
	Dictionary result;
	result["path"] = path;
	return _ok(result);
}

Dictionary Ops::h_get_theme(const Dictionary &p_args) {
	// 编辑器主题信息：Electron UI 皮肤化的数据源（当前 M1 只读投影，不做主题切换）。
	EditorSettings *es = EditorSettings::get_singleton();
	EditorInterface *ei = EditorInterface::get_singleton();
	Ref<Theme> theme = ei ? ei->get_editor_theme() : Ref<Theme>();
	Dictionary result;
	result["theme_name"] = theme.is_valid() ? String(theme->get_name()) : String();
	result["preset"] = es ? String(es->get("interface/theme/color_preset")) : String();
	Variant encoded;
	if (es && _encode_value(es->get("interface/theme/base_color"), encoded)) {
		result["base_color"] = encoded;
	}
	if (es && _encode_value(es->get("interface/theme/accent_color"), encoded)) {
		result["accent_color"] = encoded;
	}
	result["font_size"] = theme.is_valid() ? (int)theme->get_default_font_size() : 0;
	return _ok(result);
}

Dictionary Ops::h_get_scale(const Dictionary &p_args) {
	// 编辑器 UI 缩放（EDSCALE）：Electron 侧换算字体/控件尺寸的基线。
	Dictionary result;
	EditorInterface *ei = EditorInterface::get_singleton();
	result["scale"] = ei ? (double)ei->get_editor_scale() : 1.0;
	return _ok(result);
}

Dictionary Ops::h_get_project_info(const Dictionary &p_args) {
	// 项目信息：Electron 标题栏/关于面板数据源；全部来自 ProjectSettings/Engine 只读查询。
	ProjectSettings *ps = ProjectSettings::get_singleton();
	Dictionary result;
	result["project_name"] = ps ? String(ps->get_setting("application/config/name")) : String();
	result["main_scene"] = ps ? String(ps->get_setting("application/run/main_scene")) : String();
	result["rendering_method"] = ps ? String(ps->get_setting("rendering/renderer/rendering_method")) : String();
	result["project_path"] = ps ? ps->globalize_path("res://") : String();
	const Dictionary ver = Engine::get_singleton()->get_version_info();
	String version = String(ver.get("major", Variant())) + "." + String(ver.get("minor", Variant()));
	if (ver.has("patch")) {
		version += "." + String(ver["patch"]);
	}
	result["godot_version"] = version;
	return _ok(result);
}

Node *Ops::_resolve_node(const String &p_path, Dictionary &r_err) {
	// 路径守卫：禁止绝对路径（/ 开头）与 .. 逃逸（路径段 == ".."）。
	if (p_path.begins_with("/") || p_path.begins_with("//")) {
		r_err = _err("invalid_params", "禁止绝对路径: " + p_path);
		return nullptr;
	}
	const Vector<String> segs = p_path.split("/");
	for (const String &seg : segs) {
		if (seg == "..") {
			r_err = _err("invalid_params", "禁止路径逃逸（..）: " + p_path);
			return nullptr;
		}
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		r_err = _err("no_scene", "当前没有打开的编辑场景");
		return nullptr;
	}
	Node *target = p_path == "." ? root : root->get_node_or_null(NodePath(p_path));
	if (!target) {
		r_err = _err("invalid_node", "找不到节点: " + p_path);
		return nullptr;
	}
	// 归属校验：必须是编辑场景根自身或其子孙（get_node_or_null 接受 ".." 父级遍历可逃逸）。
	if (target != root && !root->is_ancestor_of(target)) {
		r_err = _err("invalid_node", "找不到节点: " + p_path);
		return nullptr;
	}
	return target;
}

Node3D *Ops::_resolve_node3d(const String &p_path, Dictionary &r_err) {
	Node *target = _resolve_node(p_path, r_err);
	if (!target) {
		return nullptr;
	}
	Node3D *node = Object::cast_to<Node3D>(target);
	if (!node) {
		r_err = _err("invalid_node", "节点不是 Node3D: " + p_path);
		return nullptr;
	}
	return node;
}

Dictionary Ops::serialize_tree(Node *p_scene_root) {
	return _tree_dict(p_scene_root, p_scene_root);
}

Dictionary Ops::_tree_dict(Node *p_node, Node *p_scene_root) {
	Dictionary d;
	d["path"] = p_node == p_scene_root ? String(".") : String(p_scene_root->get_path_to(p_node));
	d["name"] = p_node->get_name();
	d["type"] = p_node->get_class();
	Array children;
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		if (child->is_internal()) {
			continue;
		}
		children.append(_tree_dict(child, p_scene_root));
	}
	d["children"] = children;
	return d;
}

bool Ops::_encode_value(const Variant &p_value, Variant &r_out) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			r_out = Variant(); // → null
			return true;
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
			r_out = p_value;
			return true;
		case Variant::STRING_NAME:
			r_out = p_value.operator String();
			return true;
		case Variant::NODE_PATH:
			r_out = p_value.operator String();
			return true;
		case Variant::VECTOR2: {
			const Vector2 v = p_value;
			Dictionary d;
			d["x"] = v.x;
			d["y"] = v.y;
			r_out = d;
			return true;
		}
		case Variant::VECTOR2I: {
			const Vector2i v = p_value;
			Dictionary d;
			d["x"] = v.x;
			d["y"] = v.y;
			r_out = d;
			return true;
		}
		case Variant::VECTOR3: {
			const Vector3 v = p_value;
			Dictionary d;
			d["x"] = v.x;
			d["y"] = v.y;
			d["z"] = v.z;
			r_out = d;
			return true;
		}
		case Variant::VECTOR3I: {
			const Vector3i v = p_value;
			Dictionary d;
			d["x"] = v.x;
			d["y"] = v.y;
			d["z"] = v.z;
			r_out = d;
			return true;
		}
		case Variant::VECTOR4: {
			const Vector4 v = p_value;
			Dictionary d;
			d["x"] = v.x;
			d["y"] = v.y;
			d["z"] = v.z;
			d["w"] = v.w;
			r_out = d;
			return true;
		}
		case Variant::COLOR: {
			const Color c = p_value;
			Dictionary d;
			d["r"] = c.r;
			d["g"] = c.g;
			d["b"] = c.b;
			d["a"] = c.a;
			r_out = d;
			return true;
		}
		case Variant::RECT2: {
			const Rect2 r = p_value;
			Dictionary pos;
			pos["x"] = r.position.x;
			pos["y"] = r.position.y;
			Dictionary size;
			size["x"] = r.size.x;
			size["y"] = r.size.y;
			Dictionary d;
			d["position"] = pos;
			d["size"] = size;
			r_out = d;
			return true;
		}
		case Variant::RECT2I: {
			const Rect2i r = p_value;
			Dictionary pos;
			pos["x"] = r.position.x;
			pos["y"] = r.position.y;
			Dictionary size;
			size["x"] = r.size.x;
			size["y"] = r.size.y;
			Dictionary d;
			d["position"] = pos;
			d["size"] = size;
			r_out = d;
			return true;
		}
		case Variant::TRANSFORM2D: {
			const Transform2D t = p_value;
			Dictionary x, y, origin;
			x["x"] = t.columns[0].x;
			x["y"] = t.columns[0].y;
			y["x"] = t.columns[1].x;
			y["y"] = t.columns[1].y;
			origin["x"] = t.columns[2].x;
			origin["y"] = t.columns[2].y;
			Dictionary d;
			d["x"] = x;
			d["y"] = y;
			d["origin"] = origin;
			r_out = d;
			return true;
		}
		case Variant::TRANSFORM3D: {
			const Transform3D t = p_value;
			const Basis &b = t.basis;
			Dictionary bx, by, bz, origin;
			bx["x"] = b.rows[0].x;
			bx["y"] = b.rows[1].x;
			bx["z"] = b.rows[2].x;
			by["x"] = b.rows[0].y;
			by["y"] = b.rows[1].y;
			by["z"] = b.rows[2].y;
			bz["x"] = b.rows[0].z;
			bz["y"] = b.rows[1].z;
			bz["z"] = b.rows[2].z;
			origin["x"] = t.origin.x;
			origin["y"] = t.origin.y;
			origin["z"] = t.origin.z;
			Dictionary basis;
			basis["x"] = bx;
			basis["y"] = by;
			basis["z"] = bz;
			Dictionary d;
			d["basis"] = basis;
			d["origin"] = origin;
			r_out = d;
			return true;
		}
		default:
			return false; // Array/Dictionary/Resource/枚举等不可编码
	}
}

bool Ops::_decode_value(const Variant &p_value, Variant::Type p_type, Variant &r_out) {
	switch (p_type) {
		case Variant::BOOL:
			if (p_value.get_type() != Variant::BOOL) {
				return false;
			}
			r_out = p_value;
			return true;
		case Variant::INT:
			if (p_value.get_type() != Variant::INT) {
				return false;
			}
			r_out = p_value;
			return true;
		case Variant::FLOAT:
			if (p_value.get_type() == Variant::INT) {
				r_out = (double)(int64_t)p_value; // INT 可作 FLOAT 源
				return true;
			}
			if (p_value.get_type() == Variant::FLOAT && Math::is_finite((double)p_value)) {
				r_out = p_value;
				return true;
			}
			return false;
		case Variant::STRING:
			if (p_value.get_type() != Variant::STRING) {
				return false;
			}
			r_out = p_value;
			return true;
		case Variant::STRING_NAME:
			if (p_value.get_type() != Variant::STRING) {
				return false;
			}
			r_out = StringName(p_value.operator String());
			return true;
		case Variant::NODE_PATH:
			if (p_value.get_type() != Variant::STRING) {
				return false;
			}
			r_out = NodePath(p_value.operator String());
			return true;
		case Variant::VECTOR2: {
			double x, y;
			if (p_value.get_type() != Variant::DICTIONARY || !_num2_from_dict(p_value.operator Dictionary(), false, x, y)) {
				return false;
			}
			r_out = Vector2(x, y);
			return true;
		}
		case Variant::VECTOR2I: {
			double x, y;
			if (p_value.get_type() != Variant::DICTIONARY || !_num2_from_dict(p_value.operator Dictionary(), true, x, y)) {
				return false;
			}
			r_out = Vector2i((int64_t)x, (int64_t)y);
			return true;
		}
		case Variant::VECTOR3: {
			double x, y, z;
			if (p_value.get_type() != Variant::DICTIONARY || !_num3_from_dict(p_value.operator Dictionary(), false, x, y, z)) {
				return false;
			}
			r_out = Vector3(x, y, z);
			return true;
		}
		case Variant::VECTOR3I: {
			double x, y, z;
			if (p_value.get_type() != Variant::DICTIONARY || !_num3_from_dict(p_value.operator Dictionary(), true, x, y, z)) {
				return false;
			}
			r_out = Vector3i((int64_t)x, (int64_t)y, (int64_t)z);
			return true;
		}
		case Variant::VECTOR4: {
			double x, y, z, w;
			if (p_value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const Dictionary d = p_value.operator Dictionary();
			if (!_num3_from_dict(d, false, x, y, z) || !_num_member(d, "w", false, w)) {
				return false;
			}
			r_out = Vector4(x, y, z, w);
			return true;
		}
		case Variant::COLOR: {
			if (p_value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const Dictionary d = p_value.operator Dictionary();
			double r, g, b, a;
			if (!_num_member(d, "r", false, r) || !_num_member(d, "g", false, g) || !_num_member(d, "b", false, b) || !_num_member(d, "a", false, a)) {
				return false;
			}
			r_out = Color(r, g, b, a);
			return true;
		}
		case Variant::RECT2:
		case Variant::RECT2I: {
			if (p_value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const bool ints = p_type == Variant::RECT2I;
			const Dictionary d = p_value.operator Dictionary();
			const Variant pos = d.get("position", Variant());
			const Variant size = d.get("size", Variant());
			if (pos.get_type() != Variant::DICTIONARY || size.get_type() != Variant::DICTIONARY) {
				return false;
			}
			double px, py, sx, sy;
			if (!_num2_from_dict(pos.operator Dictionary(), ints, px, py) || !_num2_from_dict(size.operator Dictionary(), ints, sx, sy)) {
				return false;
			}
			if (ints) {
				r_out = Rect2i((int64_t)px, (int64_t)py, (int64_t)sx, (int64_t)sy);
			} else {
				r_out = Rect2(px, py, sx, sy);
			}
			return true;
		}
		case Variant::TRANSFORM2D: {
			if (p_value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const Dictionary d = p_value.operator Dictionary();
			const Variant x = d.get("x", Variant());
			const Variant y = d.get("y", Variant());
			const Variant origin = d.get("origin", Variant());
			if (x.get_type() != Variant::DICTIONARY || y.get_type() != Variant::DICTIONARY || origin.get_type() != Variant::DICTIONARY) {
				return false;
			}
			double xx, xy, yx, yy, ox, oy;
			if (!_num2_from_dict(x.operator Dictionary(), false, xx, xy) || !_num2_from_dict(y.operator Dictionary(), false, yx, yy) || !_num2_from_dict(origin.operator Dictionary(), false, ox, oy)) {
				return false;
			}
			Transform2D t2d;
			t2d.columns[0] = Vector2(xx, xy); // x 轴列向量 = {x,y}（与 _encode_value 互逆）
			t2d.columns[1] = Vector2(yx, yy);
			t2d.columns[2] = Vector2(ox, oy);
			r_out = t2d;
			return true;
		}
		case Variant::TRANSFORM3D: {
			if (p_value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const Dictionary d = p_value.operator Dictionary();
			const Variant basis = d.get("basis", Variant());
			const Variant origin = d.get("origin", Variant());
			if (basis.get_type() != Variant::DICTIONARY || origin.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const Dictionary bd = basis.operator Dictionary();
			const Variant bx = bd.get("x", Variant());
			const Variant by = bd.get("y", Variant());
			const Variant bz = bd.get("z", Variant());
			if (bx.get_type() != Variant::DICTIONARY || by.get_type() != Variant::DICTIONARY || bz.get_type() != Variant::DICTIONARY) {
				return false;
			}
			double bxx, bxy, bxz, byx, byy, byz, bzx, bzy, bzz, ox, oy, oz;
			if (!_num3_from_dict(bx.operator Dictionary(), false, bxx, bxy, bxz) || !_num3_from_dict(by.operator Dictionary(), false, byx, byy, byz) || !_num3_from_dict(bz.operator Dictionary(), false, bzx, bzy, bzz) || !_num3_from_dict(origin.operator Dictionary(), false, ox, oy, oz)) {
				return false;
			}
			Basis b3;
			b3.set_column(0, Vector3(bxx, bxy, bxz)); // basis.x 列向量 = {x,y,z}（与 _encode_value 互逆）
			b3.set_column(1, Vector3(byx, byy, byz));
			b3.set_column(2, Vector3(bzx, bzy, bzz));
			r_out = Transform3D(b3, Vector3(ox, oy, oz));
			return true;
		}
		default:
			return false; // 契约外类型（Array/Dictionary/Resource/枚举等）不可解码
	}
}

Dictionary Ops::h_set_window_rect(const Dictionary &p_args) {
	// C-lite 视口几何同步：Electron 主窗口移动/缩放/布局变化时，宿主经 WS 下发视口矩形，
	// 驱动嵌入窗口移动/缩放（上游 embedded 模式禁止自移动，fork 已放开——
	// 见 platform/windows/display_server_windows.cpp window_set_position/window_set_size）。
	if (!Engine::get_singleton()->is_embedded_in_editor()) {
		return _err("not_embedded", "viewport.set_window_rect 仅支持 --wid 嵌入模式");
	}
	const Variant vx = p_args.get("x", Variant());
	const Variant vy = p_args.get("y", Variant());
	const Variant vw = p_args.get("w", Variant());
	const Variant vh = p_args.get("h", Variant());
	const Variant *vals[4] = { &vx, &vy, &vw, &vh };
	for (int i = 0; i < 4; i++) {
		const Variant::Type t = vals[i]->get_type();
		if (t != Variant::FLOAT && t != Variant::INT) {
			return _err("invalid_params", "rect 必须为 {x,y,w,h} 有限数字");
		}
	}
	const double fx = vx, fy = vy, fw = vw, fh = vh;
	if (!Math::is_finite(fx) || !Math::is_finite(fy) || !Math::is_finite(fw) || !Math::is_finite(fh)) {
		return _err("invalid_params", "rect 必须为 {x,y,w,h} 有限数字");
	}
	DisplayServer *ds = DisplayServer::get_singleton();
	ds->window_set_position(Point2i((int)fx, (int)fy), DisplayServerEnums::MAIN_WINDOW_ID);
	ds->window_set_size(Size2i(MAX((int)fw, 1), MAX((int)fh, 1)), DisplayServerEnums::MAIN_WINDOW_ID);
	return _ok(Dictionary());
}

Dictionary Ops::h_set_no_focus(const Dictionary &p_args) {
	// C-lite 启动期焦点死锁规避：嵌入窗口初始 no-focus（--embedded-no-focus），
	// 编辑器 ready 后由 Electron 经此解除（window_set_flag 运行时切换已内置）。
	const Variant v = p_args.get("enabled", Variant());
	if (v.get_type() != Variant::BOOL) {
		return _err("invalid_params", "enabled 必须为布尔");
	}
	DisplayServer::get_singleton()->window_set_flag(DisplayServerEnums::WINDOW_FLAG_NO_FOCUS, v.operator bool(), DisplayServerEnums::MAIN_WINDOW_ID);
	return _ok(Dictionary());
}

#endif // TOOLS_ENABLED
