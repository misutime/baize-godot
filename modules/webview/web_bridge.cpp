// SPDX-License-Identifier: MIT

#include "web_bridge.h"

#include "webview_manager.h"

#include "core/io/json.h"
#include "core/object/object.h"
#include "core/string/print_string.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/3d/node_3d.h"
#include "scene/main/node.h"

// 桥协议方法注册表实现（协议规范见《WebUI架构-桥协议与前端SDK.md》§3）。
// 参数约定：args[0] = 前端 SDK 序列化的参数对象 JSON（含 req_id）。

void WebBridge::handle_invoke(int32_t p_browser_id, const String &p_method, const Vector<String> &p_args) {
	// 统一参数校验:协议要求 args[0] 为含字符串 req_id 的对象 JSON。
	// 无效载荷拒绝分派(不触发有副作用的方法),应答 invalid_params。
	String args_json;
	String req_id;
	const Variant parsed = JSON::parse_string(p_args.is_empty() ? "{}" : p_args[0]);
	if (parsed.get_type() == Variant::DICTIONARY) {
		const Dictionary dict = parsed.operator Dictionary();
		const Variant req_var = dict.get("req_id", Variant());
		if (req_var.get_type() == Variant::STRING) {
			req_id = req_var.operator String();
		} else {
			_respond(p_browser_id, String(), false, Variant(), "invalid_params", "req_id 必须为字符串");
			return;
		}
		args_json = p_args[0];
	} else {
		_respond(p_browser_id, String(), false, Variant(), "invalid_params", "参数必须是对象 JSON(含 req_id)");
		return;
	}
	if (p_method == "scene.get_node_count") {
		_method_scene_get_node_count(p_browser_id, args_json);
	} else if (p_method == "scene.create_node") {
		_method_scene_create_node(p_browser_id, args_json);
	} else if (p_method == "editor.undo") {
		_method_editor_undo(p_browser_id, args_json);
	} else if (p_method == "editor.redo") {
		_method_editor_redo(p_browser_id, args_json);
	} else {
		_respond(p_browser_id, req_id, false, Variant(), "method_not_found", "未注册的方法: " + p_method);
	}
}

void WebBridge::_method_scene_get_node_count(int32_t p_browser_id, const String &p_args_json) {
	// 参数: { req_id }。返回场景根节点后代总数(含根自身)。
	String req_id;
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		req_id = parsed.operator Dictionary().get("req_id", "").operator String();
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		_respond(p_browser_id, req_id, false, Variant(), "no_scene", "当前没有打开的编辑场景");
		return;
	}
	// 递归统计(含根自身)。
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
	_respond(p_browser_id, req_id, true, count);
}

void WebBridge::_method_scene_create_node(int32_t p_browser_id, const String &p_args_json) {
	// 参数: { req_id, name }。创建 Node3D 并作为编辑场景根的子节点(undo 可撤销)。
	String req_id;
	String name = "BridgeNode";
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		const Dictionary args = parsed.operator Dictionary();
		req_id = args.get("req_id", "").operator String();
		name = args.get("name", name).operator String();
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		_respond(p_browser_id, req_id, false, Variant(), "no_scene", "当前没有打开的编辑场景");
		return;
	}
	Node3D *node = memnew(Node3D);
	node->set_name(name);
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("WebUI Create Node");
	eurm->add_do_method(root, "add_child", node, true);
	eurm->add_undo_method(root, "remove_child", node);
	// 场景 owner:未设 owner 的节点不会写入 .tscn,保存场景后丢失(PackedScene::_parse_node 跳过);
	// redo 时同样需要恢复 owner(与 SceneTreeDock 创建路径一致)。
	eurm->add_do_method(node, "set_owner", root);
	// UndoRedo 接管释放:undo 后 remove_child 使节点脱离场景,redo 栈被丢弃时
	// (discard_redo) 由 TYPE_REFERENCE 引用操作删除——否则节点永久泄漏。
	eurm->add_do_reference(node);
	eurm->commit_action();
	// node 已由 do 方法(commit_action 执行)挂入场景;返回 instance_id 作为 node_id。
	_respond(p_browser_id, req_id, true, static_cast<uint64_t>(node->get_instance_id()));
}

void WebBridge::_method_editor_undo(int32_t p_browser_id, const String &p_args_json) {
	String req_id;
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		req_id = parsed.operator Dictionary().get("req_id", "").operator String();
	}
	const bool ok = EditorUndoRedoManager::get_singleton()->undo();
	if (!ok) {
		_respond(p_browser_id, req_id, false, Variant(), "nothing_to_undo", "没有可撤销的操作");
		return;
	}
	_respond(p_browser_id, req_id, true, Dictionary());
}

void WebBridge::_method_editor_redo(int32_t p_browser_id, const String &p_args_json) {
	String req_id;
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		req_id = parsed.operator Dictionary().get("req_id", "").operator String();
	}
	const bool ok = EditorUndoRedoManager::get_singleton()->redo();
	if (!ok) {
		_respond(p_browser_id, req_id, false, Variant(), "nothing_to_redo", "没有可重做的操作");
		return;
	}
	_respond(p_browser_id, req_id, true, Dictionary());
}

void WebBridge::_respond(int32_t p_browser_id, const String &p_req_id, bool p_ok, const Variant &p_result,
		const String &p_error_code, const String &p_error_message) {
	Dictionary body;
	body["req_id"] = p_req_id;
	if (p_ok) {
		body["ok"] = true;
		body["result"] = p_result;
	} else {
		body["ok"] = false;
		Dictionary error;
		error["code"] = p_error_code;
		error["message"] = p_error_message;
		body["error"] = error;
	}
	emit_event(p_browser_id, "method_result", JSON::stringify(body));
}

void WebBridge::emit_event(int32_t p_browser_id, const String &p_event_name, const String &p_payload_json) {
	WebViewManager *mgr = WebViewManager::peek_singleton();
	if (!mgr) {
		return; // teardown 后不复活单例
	}
	mgr->emit_event(p_browser_id, p_event_name, p_payload_json);
}
