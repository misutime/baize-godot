// SPDX-License-Identifier: MIT
#include "ops.h"

#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/3d/node_3d.h"

#ifdef TOOLS_ENABLED

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

// ---- 内部工具 ----

Node3D *Ops::_resolve_node3d(const String &p_path, Dictionary &r_err) {
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
	Node3D *node = Object::cast_to<Node3D>(target);
	if (!node || (target != root && !root->is_ancestor_of(target))) {
		r_err = _err("invalid_node", "找不到节点或节点不是 Node3D: " + p_path);
		return nullptr;
	}
	return node;
}

#endif // TOOLS_ENABLED
