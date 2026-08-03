// SPDX-License-Identifier: MIT

#include "web_bridge.h"

#include "webview_manager.h"

#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "core/string/print_string.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
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
	} else if (p_method == "scene.get_node_position") {
		_method_scene_get_node_position(p_browser_id, args_json);
	} else if (p_method == "scene.set_node_position") {
		_method_scene_set_node_position(p_browser_id, args_json);
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

void WebBridge::_method_scene_get_node_position(int32_t p_browser_id, const String &p_args_json) {
	// 参数: { req_id, node_path }。返回 Node3D 位置 {x,y,z}。
	// node_path 为场景相对路径(与 selection_changed 的 node_paths 语义一致,"."=场景根)。
	String req_id;
	String node_path;
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		const Dictionary args = parsed.operator Dictionary();
		req_id = args.get("req_id", "").operator String();
		const Variant path_var = args.get("node_path", Variant());
		if (path_var.get_type() == Variant::STRING) {
			node_path = path_var.operator String();
		}
	}
	Node3D *node = _resolve_node3d(p_browser_id, req_id, node_path);
	if (!node) {
		return; // 错误应答已由 _resolve_node3d 发出
	}
	const Vector3 pos = node->get_position();
	Dictionary result;
	result["x"] = pos.x;
	result["y"] = pos.y;
	result["z"] = pos.z;
	_respond(p_browser_id, req_id, true, result);
}

void WebBridge::_method_scene_set_node_position(int32_t p_browser_id, const String &p_args_json) {
	// 参数: { req_id, node_path, position: {x,y,z} }。设置 Node3D 位置(undo 可撤销)。
	// position 与协议 §3.3 node_position_changed 载荷同构;
	// undo 入 EditorUndoRedoManager,与 scene.create_node 同一撤销栈。
	String req_id;
	String node_path;
	Vector3 new_pos;
	bool has_position = false;
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		const Dictionary args = parsed.operator Dictionary();
		req_id = args.get("req_id", "").operator String();
		const Variant path_var = args.get("node_path", Variant());
		if (path_var.get_type() == Variant::STRING) {
			node_path = path_var.operator String();
		}
		const Variant pos_var = args.get("position", Variant());
		if (pos_var.get_type() == Variant::DICTIONARY) {
			const Dictionary pos = pos_var.operator Dictionary();
			// JSON 整数字面量解析为 INT、带小数点为 FLOAT,两者都接受。
			const Variant x = pos.get("x", Variant());
			const Variant y = pos.get("y", Variant());
			const Variant z = pos.get("z", Variant());
			if ((x.get_type() == Variant::FLOAT || x.get_type() == Variant::INT) &&
					(y.get_type() == Variant::FLOAT || y.get_type() == Variant::INT) &&
					(z.get_type() == Variant::FLOAT || z.get_type() == Variant::INT)) {
				// 有限性校验:JSON 溢出数字(如 1e400)经 String::to_float 得 +inf,
				// 直接入栈会把非有限变换写进场景节点(审查 P2)。
				const double fx = x;
				const double fy = y;
				const double fz = z;
				if (Math::is_finite(fx) && Math::is_finite(fy) && Math::is_finite(fz)) {
					new_pos = Vector3(fx, fy, fz);
					has_position = true;
				}
			}
		}
	}
	if (!has_position) {
		_respond(p_browser_id, req_id, false, Variant(), "invalid_params", "position 必须为 {x,y,z} 有限数字");
		return;
	}
	Node3D *node = _resolve_node3d(p_browser_id, req_id, node_path);
	if (!node) {
		return; // 错误应答已由 _resolve_node3d 发出
	}
	const Vector3 old_pos = node->get_position();
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	eurm->create_action("WebUI Set Position");
	eurm->add_do_method(node, "set_position", new_pos);
	eurm->add_undo_method(node, "set_position", old_pos);
	eurm->commit_action();
	_respond(p_browser_id, req_id, true, Dictionary());
}

Node3D *WebBridge::_resolve_node3d(int32_t p_browser_id, const String &p_req_id, const String &p_node_path) {
	// 场景相对路径 → Node3D 的公共解析(错误应答集中处理)。
	if (p_node_path.is_empty()) {
		_respond(p_browser_id, p_req_id, false, Variant(), "invalid_params", "node_path 必须为非空字符串");
		return nullptr;
	}
	const NodePath path(p_node_path);
	// 协议约定 node_path 为编辑场景相对路径:拒绝绝对路径(以 / 开头,
	// get_node_or_null 会从 SceneTree 根解析,可指向编辑器内部节点)。
	if (path.is_absolute()) {
		_respond(p_browser_id, p_req_id, false, Variant(), "invalid_params", "node_path 必须为场景相对路径(不得以 / 开头)");
		return nullptr;
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	if (!root) {
		_respond(p_browser_id, p_req_id, false, Variant(), "no_scene", "当前没有打开的编辑场景");
		return nullptr;
	}
	Node3D *node = Object::cast_to<Node3D>(root->get_node_or_null(path));
	// 归属校验:解析结果必须是编辑场景根自身或其子孙。get_node_or_null 接受
	// ".." 父级遍历,可逃逸到编辑场景根之外(编辑器内部节点)——拒绝,
	// 否则 setter 会把对内部节点的修改记录成场景编辑。
	if (!node || (node != root && !root->is_ancestor_of(node))) {
		_respond(p_browser_id, p_req_id, false, Variant(), "invalid_node", "找不到节点或节点不是 Node3D: " + p_node_path);
		return nullptr;
	}
	return node;
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

// ---- 事件源（MVP2 后半；机制见《WebUI架构-桥协议与前端SDK.md》§6）----

// 事件下行目标：单 WebDock 单浏览器（多面板时改注册表）。
int32_t WebBridge::event_browser_id_ = -1;
bool WebBridge::event_sources_connected_ = false;
HashMap<ObjectID, Vector3> WebBridge::tracked_positions_;
bool WebBridge::last_can_undo_ = true; // 哨兵：首帧 diff 必发一次当前状态
bool WebBridge::last_can_redo_ = true;

void WebBridge::set_event_browser_id(int32_t p_browser_id) {
	event_browser_id_ = p_browser_id;
	tracked_positions_.clear(); // 目标切换：清空基线，防旧节点缓存误发
	// 重置 undo 哨兵：新消费者首帧 poll 收到当前栈状态（否则旧值 diff 吞掉状态）。
	last_can_undo_ = true;
	last_can_redo_ = true;
}

void WebBridge::init_event_sources() {
	if (event_sources_connected_) {
		return;
	}
	EditorNode *ed = EditorNode::get_singleton();
	if (!ed) {
		return; // 启动时序未就绪（dock 注册发生在 Main::start 后，正常不应出现）
	}
	ed->get_editor_selection()->connect("selection_changed", callable_mp_static(&WebBridge::_on_selection_changed));
	event_sources_connected_ = true;
	// 初始同步一次当前选中：此时浏览器 id 可能未注册（面板刚创建），
	// 事件发不出但基线会建立；前端订阅前的首次真实选中仍会触发。
	_on_selection_changed();
}

void WebBridge::deinit_event_sources() {
	if (!event_sources_connected_) {
		return;
	}
	EditorNode *ed = EditorNode::get_singleton();
	if (ed && ed->get_editor_selection()) {
		ed->get_editor_selection()->disconnect("selection_changed", callable_mp_static(&WebBridge::_on_selection_changed));
	}
	event_sources_connected_ = false;
}

void WebBridge::poll_editor_state() {
	if (event_browser_id_ < 0) {
		return; // 无事件下行目标（浏览器未就绪/已注销）
	}
	_refresh_tracked_positions(false); // 纯 diff：位置变化才发
	_poll_undo_stack();
}

void WebBridge::emit_initial_state() {
	// 页面加载完成（订阅就绪）后下发完整初始状态：当前选中 node_paths +
	// 各选中节点初始位置 + 下帧 undo 栈状态（哨兵重置）。
	// 场景：选中先于浏览器就绪/页面订阅时，事件会被跳过（browser_id<0 早退），
	// 消费者将永远收不到初始值——必须在此强制快照。
	tracked_positions_.clear(); // 强制全部选中节点按"新节点"发初始位置
	last_can_undo_ = true; // 下帧 poll 发实际栈状态
	last_can_redo_ = true;
	_on_selection_changed();
}

void WebBridge::_on_selection_changed() {
	EditorNode *ed = EditorNode::get_singleton();
	if (!ed) {
		return;
	}
	if (event_browser_id_ >= 0) {
		// 下行 selection_changed：node_paths = 全部选中节点的场景相对路径
		// （相对编辑场景根；get_path() 会返回编辑器内部全局路径，前端无法定位）。
		Array node_paths;
		List<Node *> nodes = ed->get_editor_selection()->get_full_selected_node_list();
		Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
		for (const Node *n : nodes) {
			// 统一场景相对路径语义（NodePath::get_path_to）：根节点自身为 "."，
			// 前端 SDK 约定 "." = 场景根（与 Godot NodePath 语义一致）。
			node_paths.append(scene_root ? scene_root->get_path_to(n) : NodePath(n->get_name()));
		}
		Dictionary body;
		body["node_paths"] = node_paths;
		emit_event(event_browser_id_, "editor.selection_changed", JSON::stringify(body));
	}
	// 重建位置跟踪基线：新选中节点立即发初始位置（验收 2：选中即显示 X）。
	_refresh_tracked_positions(event_browser_id_ >= 0);
}

void WebBridge::_refresh_tracked_positions(bool p_emit_initial_for_new) {
	EditorNode *ed = EditorNode::get_singleton();
	if (!ed) {
		return;
	}
	HashMap<ObjectID, Vector3> next;
	List<Node *> nodes = ed->get_editor_selection()->get_full_selected_node_list();
	for (Node *n : nodes) {
		Node3D *n3 = Object::cast_to<Node3D>(n);
		if (!n3) {
			continue; // 仅 Node3D 有 position(Vector3) 语义；Control/Node2D 后续扩展
		}
		const ObjectID id = n->get_instance_id();
		const Vector3 pos = n3->get_position();
		HashMap<ObjectID, Vector3>::Iterator it = tracked_positions_.find(id);
		if (!it) {
			if (p_emit_initial_for_new) {
				_emit_node_position_changed(id, pos);
			}
			next[id] = pos;
		} else {
			const Vector3 &last = it->value;
			// 帧轮询 diff（RouteB 决策 #5：阈值 1e-6，只在变化时推送）。
			if (Math::abs(pos.x - last.x) > 1e-6 || Math::abs(pos.y - last.y) > 1e-6 || Math::abs(pos.z - last.z) > 1e-6) {
				_emit_node_position_changed(id, pos);
			}
			// 未变化/亚阈值：保留旧基线进 next——否则缓存清空，后续变化被当"新节点"
			// （p_emit_initial_for_new=false 时不发），累积亚阈值移动会被静默吞掉。
			next[id] = last;
		}
	}
	tracked_positions_ = next; // 未选中的节点自然从跟踪中移除
}

void WebBridge::_emit_node_position_changed(ObjectID p_node_id, const Vector3 &p_position) {
	if (event_browser_id_ < 0) {
		return;
	}
	Dictionary body;
	body["node_id"] = static_cast<uint64_t>(p_node_id); // instance_id 即 node_id（与 create_node 返回一致）
	// position 必须构造 {x,y,z} 对象：JSON::stringify 对 Vector3 直接序列化为
	// 字符串 "(x, y, z)"（Variant toString），与协议 §3.3 的对象定义不符。
	Dictionary pos;
	pos["x"] = p_position.x;
	pos["y"] = p_position.y;
	pos["z"] = p_position.z;
	body["position"] = pos;
	emit_event(event_browser_id_, "editor.node_position_changed", JSON::stringify(body));
}

void WebBridge::_poll_undo_stack() {
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	const bool can_undo = eurm->has_undo();
	const bool can_redo = eurm->has_redo();
	if (can_undo == last_can_undo_ && can_redo == last_can_redo_) {
		return;
	}
	last_can_undo_ = can_undo;
	last_can_redo_ = can_redo;
	Dictionary body;
	body["can_undo"] = can_undo;
	body["can_redo"] = can_redo;
	emit_event(event_browser_id_, "editor.undo_stack_changed", JSON::stringify(body));
}
