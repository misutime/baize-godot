// SPDX-License-Identifier: MIT
#include "provider_server.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/string/print_string.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "ops.h"
#include "registry.h"
#include "scene/main/scene_tree.h"

#ifdef TOOLS_ENABLED

ProviderServer *ProviderServer::singleton = nullptr;

ProviderServer *ProviderServer::get_singleton() {
	if (!singleton) {
		singleton = memnew(ProviderServer);
	}
	return singleton;
}

void ProviderServer::free_singleton() {
	if (singleton) {
		singleton->stop();
		memdelete(singleton);
		singleton = nullptr;
	}
}

// ---- 启动 / 停止 ----

void ProviderServer::start() {
	if (started_) {
		return;
	}
	// 端口：默认 23009（MVP 简化：固定端口便于联调）；env BAIZE_PROVIDER_PORT 覆盖。
	const char *env_port = std::getenv("BAIZE_PROVIDER_PORT");
	const int port = env_port ? String(env_port).to_int() : 23009;

	// token：env BAIZE_PROVIDER_TOKEN（Electron spawn 时下发）；缺失 = dev 宽松模式（警告 + 跳过校验）。
	const char *env_token = std::getenv("BAIZE_PROVIDER_TOKEN");
	token_ = env_token ? String(env_token) : String();
	if (token_.is_empty()) {
		print_line("[gd_provider] BAIZE_PROVIDER_TOKEN 未设置——dev 宽松模式（不校验 hello token）");
	}

	tcp_server_.instantiate();
	Error err = tcp_server_->listen(port, IPAddress("127.0.0.1"));
	if (err != OK) {
		ERR_PRINT("[gd_provider] 启动失败：无法监听 127.0.0.1:" + itos(port) + "（错误 " + itos(err) + "）");
		tcp_server_ = Ref<TCPServer>();
		return;
	}
	listen_port_ = tcp_server_->get_local_port();

	if (!start_frame_pump()) {
		ERR_PRINT("[gd_provider] SceneTree 未就绪，放弃启动");
		stop();
		return;
	}
	started_ = true;

	// 事件源：EditorSelection 信号（选中变化）+ EditorUndoRedoManager 双信号——
	// history_changed（commit_action 成功，覆盖所有 do 操作：能力面 mutation 与原生 UI 编辑）+
	// version_changed（undo/redo 成功）。两者互补不重叠（commit 与 undo 互斥），避免重复推送。
	EditorNode *ed = EditorNode::get_singleton();
	if (ed && ed->get_editor_selection()) {
		ed->get_editor_selection()->connect("selection_changed", callable_mp(this, &ProviderServer::_on_selection_changed));
	}
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	if (eurm) {
		eurm->connect("history_changed", callable_mp(this, &ProviderServer::_on_undo_version_changed));
		eurm->connect("version_changed", callable_mp(this, &ProviderServer::_on_undo_version_changed));
	}

	print_line("[gd_provider] WS server 就绪: ws://127.0.0.1:" + itos(listen_port_));
}

void ProviderServer::stop() {
	stop_frame_pump();
	// 断开事件源信号（stop→start 防重复 connect/事件丢失，review P3）
	EditorNode *ed = EditorNode::get_singleton();
	if (ed && ed->get_editor_selection()) {
		ed->get_editor_selection()->disconnect("selection_changed", callable_mp(this, &ProviderServer::_on_selection_changed));
	}
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	if (eurm) {
		if (eurm->is_connected("history_changed", callable_mp(this, &ProviderServer::_on_undo_version_changed))) {
			eurm->disconnect("history_changed", callable_mp(this, &ProviderServer::_on_undo_version_changed));
		}
		if (eurm->is_connected("version_changed", callable_mp(this, &ProviderServer::_on_undo_version_changed))) {
			eurm->disconnect("version_changed", callable_mp(this, &ProviderServer::_on_undo_version_changed));
		}
	}
	// shutdown 通知（P2 review）：告知已认证客户端主动断开（否则 client 只靠断开重连退避）
	for (Peer &p : peers_) {
		if (p.authenticated && p.peer.is_valid() && !p.dead) {
			Dictionary notify;
			notify["jsonrpc"] = "2.0";
			notify["method"] = "shutdown";
			_send(p, notify);
		}
	}
	for (Peer &p : peers_) {
		_flush_out(p);
	}
	if (tcp_server_.is_valid()) {
		tcp_server_->stop();
		tcp_server_ = Ref<TCPServer>();
	}
	for (Peer &peer : peers_) {
		if (peer.peer.is_valid()) {
			peer.peer->close();
		}
	}
	peers_.clear();
	_tracked_selection_ = Array();
	_tracked_positions_.clear();
	_tree_signature_ = String();
	_tree_dirty_ = true;
	_last_tree_check_ms_ = 0;
	_can_undo_ = false;
	_can_redo_ = false;
	started_ = false;
}

bool ProviderServer::start_frame_pump() {
	if (pump_driver_) {
		return true;
	}
	SceneTree *st = SceneTree::get_singleton();
	if (!st) {
		return false;
	}
	pump_driver_ = st;
	st->connect("process_frame", callable_mp(this, &ProviderServer::poll));
	return true;
}

void ProviderServer::stop_frame_pump() {
	if (!pump_driver_) {
		return;
	}
	SceneTree *st = SceneTree::get_singleton();
	if (st && st->is_connected("process_frame", callable_mp(this, &ProviderServer::poll))) {
		st->disconnect("process_frame", callable_mp(this, &ProviderServer::poll));
	}
	pump_driver_ = nullptr;
}

// ---- 连接处理 ----

void ProviderServer::poll() {
	if (!started_ || tcp_server_.is_null()) {
		return;
	}
	// C-lite：就绪标志（EditorNode 构造完成 = 主循环已起，消息循环稳定泵送）。
	if (!editor_ready_ && EditorNode::get_singleton()) {
		editor_ready_ = true;
	}
	_accept_connections();
	_poll_peers();
	_poll_state_diff();
}

void ProviderServer::_accept_connections() {
	while (tcp_server_->is_connection_available()) {
		if (peers_.size() >= MAX_CLIENTS) {
			Ref<StreamPeer> sp = tcp_server_->take_connection();
			if (sp.is_valid()) {
				Ref<StreamPeerTCP> tcp = sp;
				if (tcp.is_valid()) {
					tcp->disconnect_from_host();
				}
			}
			continue;
		}
		Ref<WebSocketPeer> peer = Ref<WebSocketPeer>(WebSocketPeer::create());
		if (peer.is_null()) {
			Ref<StreamPeer> sp = tcp_server_->take_connection();
			if (sp.is_valid()) {
				Ref<StreamPeerTCP> tcp = sp;
				if (tcp.is_valid()) {
					tcp->disconnect_from_host();
				}
			}
			continue;
		}
		Error err = peer->accept_stream(tcp_server_->take_connection());
		if (err != OK) {
			continue;
		}
		Peer p;
		p.peer = peer;
		p.auth_deadline_ms = OS::get_singleton()->get_ticks_msec() + AUTH_TIMEOUT_MS;
		peers_.push_back(p);
	}
}

void ProviderServer::_poll_peers() {
	for (int i = peers_.size() - 1; i >= 0; i--) {
		Peer &sp = peers_.write[i];
		if (sp.peer.is_null()) {
			peers_.remove_at(i);
			continue;
		}
		sp.peer->poll();
		const WebSocketPeer::State state = sp.peer->get_ready_state();
		if (state == WebSocketPeer::STATE_CLOSED || state == WebSocketPeer::STATE_CLOSING) {
			_drop_peer(i, "连接关闭");
			continue;
		}
		if (!sp.authenticated && sp.auth_deadline_ms != 0 && OS::get_singleton()->get_ticks_msec() > sp.auth_deadline_ms) {
			ERR_PRINT("[gd_provider] 连接认证超时（3s），断开");
			_drop_peer(i, "认证超时");
			continue;
		}
		int msgs = 0;
		while (!sp.dead && sp.peer.is_valid() && sp.peer->get_available_packet_count() > 0 && msgs < MAX_MSGS_PER_FRAME) {
			const uint8_t *data = nullptr;
			int size = 0;
			const Error perr = sp.peer->get_packet(&data, size);
			if (perr != OK || data == nullptr) {
				sp.dead = true;
				sp.drop_reason = "读帧失败";
				break;
			}
			if (!sp.peer->was_string_packet()) {
				sp.dead = true;
				sp.drop_reason = "非文本帧";
				break;
			}
			const String text = String::utf8((const char *)data, size);
			_handle_frame(sp, text);
			msgs++;
		}
		if (sp.dead) {
			_drop_peer(i, sp.drop_reason.is_empty() ? "致命状态" : sp.drop_reason);
			continue;
		}
		_flush_out(sp);
	}
}

void ProviderServer::_handle_frame(Peer &p_peer, const String &p_text) {
	// 严格 JSON-RPC 2.0 解析（与 @baize/godot-rpc codec 语义一致）。
	JSON json;
	Error err = json.parse(p_text);
	if (err != OK) {
		_send(p_peer, _jsonrpc_error_doc(-32700, "Parse error", Variant(), true));
		return;
	}
	const Variant parsed = json.get_data();
	if (parsed.get_type() == Variant::ARRAY) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: batch 显式拒绝", Variant(), true));
		return;
	}
	if (parsed.get_type() != Variant::DICTIONARY) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request", Variant(), true));
		return;
	}
	const Dictionary req = parsed.operator Dictionary();
	if (req.get("jsonrpc", Variant()) != String("2.0")) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: jsonrpc 必须为 \"2.0\"", Variant(), true));
		return;
	}
	if (req.get("method", Variant()).get_type() != Variant::STRING) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: 服务端不接受 response 输入", Variant(), true));
		return;
	}
	const String method = req["method"].operator String();
	const bool has_id = req.has("id");
	if (has_id && req["id"].get_type() != Variant::STRING) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: request id 必须为 string", Variant(), true));
		return;
	}
	const Variant id = has_id ? req["id"] : Variant();
	const Variant params = req.get("params", Variant());

	// 认证：未认证只接受 hello；token 校验失败断开 + 告警。
	if (!p_peer.authenticated) {
		if (method != "hello") {
			ERR_PRINT("[gd_provider] 未认证连接调用 " + method + "，断开");
			p_peer.dead = true;
			p_peer.drop_reason = "未认证调用: " + method;
			return;
		}
		const Dictionary p = params.get_type() == Variant::DICTIONARY ? params.operator Dictionary() : Dictionary();
		if (!token_.is_empty() && p.get("token", String()).operator String() != token_) {
			ERR_PRINT("[gd_provider] hello token 校验失败，断开");
			if (has_id) {
				Dictionary internal_error;
				internal_error["code"] = "unauthorized";
				internal_error["message"] = "token 校验失败";
				Dictionary resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				resp["error"] = _jsonrpc_error(internal_error);
				p_peer.peer->send_text(JSON::stringify(resp));
			}
			p_peer.dead = true;
			p_peer.drop_reason = "token 校验失败";
			return;
		}
		p_peer.authenticated = true;
		p_peer.auth_deadline_ms = 0;
		print_line("[gd_provider] 客户端握手成功");
		// C-lite：就绪通知——Electron 据此解除启动期焦点保护（晚连接者在此补收，幂等）。
		if (editor_ready_) {
			Dictionary notify;
			notify["jsonrpc"] = "2.0";
			notify["method"] = "editor.ready";
			notify["params"] = Dictionary();
			_send(p_peer, notify);
		}
	}

	const Dictionary result = _dispatch(p_peer, method, params);
	if (!has_id) {
		return;
	}
	if (result.get("ok", false).operator bool()) {
		Dictionary resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["result"] = result.get("result", Variant());
		_send(p_peer, resp);
	} else {
		const Dictionary error = result.get("error", Dictionary()).operator Dictionary();
		Dictionary resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["error"] = _jsonrpc_error(error);
		_send(p_peer, resp);
	}
}

void ProviderServer::_send(Peer &p_peer, const Dictionary &p_msg) {
	const String text = JSON::stringify(p_msg);
	const int bytes = text.utf8().length();
	if (p_peer.out_queue.size() * 4096 + bytes > MAX_OUT_BYTES) {
		p_peer.dead = true;
		return;
	}
	p_peer.out_queue.push_back(text);
}

void ProviderServer::_flush_out(Peer &p_peer) {
	while (!p_peer.out_queue.is_empty()) {
		const String text = p_peer.out_queue[0];
		const Error err = p_peer.peer->send_text(text);
		if (err == OK) {
			p_peer.out_queue.remove_at(0);
		} else {
			p_peer.dead = true;
			break;
		}
	}
}

void ProviderServer::_drop_peer(int p_index, const String &p_reason) {
	if (p_index < 0 || p_index >= peers_.size()) {
		return;
	}
	Peer &sp = peers_.write[p_index];
	if (sp.peer.is_valid()) {
		sp.peer->close(1008, p_reason);
	}
	peers_.remove_at(p_index);
}

// ---- 分派 ----

Dictionary ProviderServer::_dispatch(Peer &p_peer, const String &p_method, const Variant &p_params) {
	if (p_method == "hello") {
		// hello 幂等：每次调用都校验 token（已认证连接上错误 token 同样拒绝）。
		const Dictionary p = p_params.get_type() == Variant::DICTIONARY ? p_params.operator Dictionary() : Dictionary();
		if (!token_.is_empty() && p.get("token", String()).operator String() != token_) {
			Dictionary d;
			d["ok"] = false;
			Dictionary e;
			e["code"] = "unauthorized";
			e["message"] = "token 校验失败";
			d["error"] = e;
			return d;
		}
		Dictionary result;
		result["ok"] = true;
		Dictionary payload;
		payload["ok"] = true;
		payload["version"] = "0.1.0";
		result["result"] = payload;
		return result;
	}
	// Registry 透传（能力面唯一事实源）。
	const Registry::Method *m = Registry::find(p_method);
	if (!m) {
		Dictionary d;
		d["ok"] = false;
		Dictionary e;
		e["code"] = "method_not_found";
		e["message"] = "未注册的方法: " + p_method;
		d["error"] = e;
		return d;
	}
	Dictionary args;
	String verr;
	if (!Registry::validate_args(*m, p_params, args, verr)) {
		Dictionary d;
		d["ok"] = false;
		Dictionary e;
		e["code"] = "invalid_params";
		e["message"] = verr;
		d["error"] = e;
		return d;
	}
	// 事件推送由 EURM 信号驱动（history_changed/version_changed，见 start()），
	// 不再按方法名列表在 dispatch 后推——避免 undo/redo 双推（version_changed + mutation 列表重复）。
	return m->handler(args);
}

Dictionary ProviderServer::_jsonrpc_error(const Dictionary &p_internal_error) {
	const String code = p_internal_error.get("code", "").operator String();
	const String message = p_internal_error.get("message", "unknown error").operator String();
	Dictionary error;
	if (code == "method_not_found") {
		error["code"] = -32601;
	} else if (code == "invalid_params") {
		error["code"] = -32602;
	} else {
		error["code"] = -32000; // 业务失败统一码
	}
	error["message"] = message;
	Dictionary data;
	data["code"] = code; // 内部字符串码入 data，不污染数值 error.code
	error["data"] = data;
	return error;
}

Dictionary ProviderServer::_jsonrpc_error_doc(int p_code, const String &p_message, const Variant &p_data, bool p_include_id, const Variant &p_id) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	if (p_data.get_type() != Variant::NIL) {
		error["data"] = p_data;
	}
	Dictionary resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = p_include_id ? p_id : Variant();
	resp["error"] = error;
	return resp;
}

// ---- 事件源（Events 层） ----

// 树结构 diff 签名：递归拼接 name+type+子节点顺序（跳过 is_internal 子节点）。
// 无场景时由调用方固定为空串；节点名不允许含 '/' 与 ':'，拼接无歧义。
static String _tree_signature(Node *p_root) {
	String sig = String(p_root->get_name()) + ":" + p_root->get_class();
	for (int i = 0; i < p_root->get_child_count(); i++) {
		Node *child = p_root->get_child(i);
		if (child->is_internal()) {
			continue;
		}
		sig += "/" + _tree_signature(child);
	}
	return sig;
}

void ProviderServer::_push_scene_changed() {
	// 向所有已认证 peer 推送完整树（无场景 → tree: null）。
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	Dictionary payload;
	payload["tree"] = root ? Variant(Ops::serialize_tree(root)) : Variant();
	for (Peer &p : peers_) {
		if (p.authenticated && p.peer.is_valid() && !p.dead) {
			Dictionary notify;
			notify["jsonrpc"] = "2.0";
			notify["method"] = "scene.changed";
			notify["params"] = payload;
			_send(p, notify);
		}
	}
}

void ProviderServer::_push_undo_stack_changed() {
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	_can_undo_ = eurm->has_undo();
	_can_redo_ = eurm->has_redo();
	Dictionary payload;
	payload["can_undo"] = _can_undo_;
	payload["can_redo"] = _can_redo_;
	for (Peer &p : peers_) {
		if (p.authenticated && p.peer.is_valid() && !p.dead) {
			Dictionary notify;
			notify["jsonrpc"] = "2.0";
			notify["method"] = "editor.undo_stack_changed";
			notify["params"] = payload;
			_send(p, notify);
		}
	}
}

void ProviderServer::_on_undo_version_changed() {
	// EditorUndoRedoManager::version_changed：undo/redo 成功时 emit。
	// 任何动作（含原生 UI 与能力面操作）的撤销/重做都会经过它——统一触发场景与 undo 栈重推。
	_notify_scene_mutated();
}

void ProviderServer::_notify_scene_mutated() {
	// 事件信号触发：立即推送场景/undo 栈/选择事件，并同步 diff 基线
	// （避免帧轮询重复推送）；轮询保留作外部非 undo 变化的兜底。
	// 无客户端时不序列化（信号可能来自原生 UI 操作，省帧预算）——连上后客户端自行拉全量。
	if (peers_.is_empty()) {
		return;
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	_tree_signature_ = root ? _tree_signature(root) : String();
	_tree_dirty_ = false;
	_last_tree_check_ms_ = OS::get_singleton()->get_ticks_msec();
	_sync_selection();
	// 同步位置基线：mutation 已推 scene.changed，若基线不更新，下一帧 poll 会再推
	// node_position_changed（重复事件，review）。
	if (EditorNode *ed = EditorNode::get_singleton(); ed && ed->get_editor_selection()) {
		HashMap<ObjectID, Vector3> current;
		List<Node *> nodes = ed->get_editor_selection()->get_full_selected_node_list();
		for (Node *n : nodes) {
			if (Node3D *n3d = Object::cast_to<Node3D>(n)) {
				current[n->get_instance_id()] = n3d->get_position();
			}
		}
		for (const KeyValue<ObjectID, Vector3> &kv : current) {
			_tracked_positions_[kv.key] = kv.value;
		}
		Vector<ObjectID> stale;
		for (const KeyValue<ObjectID, Vector3> &kv : _tracked_positions_) {
			if (!current.has(kv.key)) {
				stale.push_back(kv.key);
			}
		}
		for (const ObjectID &id : stale) {
			_tracked_positions_.erase(id);
		}
	}
	_push_scene_changed();
	_push_undo_stack_changed();
}

void ProviderServer::_on_selection_changed() {
	// 选中变化即时推送（EditorSelection 信号；update 未绑定 ClassDB 不能入 UndoRedo action——
	// mutation/undo 后的选择变化统一由 _notify_scene_mutated → _sync_selection diff 覆盖）。
	_sync_selection();
}

void ProviderServer::_sync_selection() {
	if (peers_.is_empty()) {
		return;
	}
	EditorNode *ed = EditorNode::get_singleton();
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	Array node_paths;
	if (root && ed && ed->get_editor_selection()) {
		List<Node *> nodes = ed->get_editor_selection()->get_full_selected_node_list();
		for (Node *n : nodes) {
			node_paths.append(String(root->get_path_to(n)));
		}
	}
	if (node_paths == _tracked_selection_) {
		return; // 选择未变化：不推
	}
	_tracked_selection_ = node_paths;
	Dictionary payload;
	payload["node_paths"] = node_paths;
	for (Peer &p : peers_) {
		if (p.authenticated && p.peer.is_valid() && !p.dead) {
			Dictionary notify;
			notify["jsonrpc"] = "2.0";
			notify["method"] = "editor.selection_changed";
			notify["params"] = payload;
			_send(p, notify);
		}
	}
}

void ProviderServer::_poll_state_diff() {
	if (peers_.is_empty()) {
		return;
	}
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	EditorNode *ed = EditorNode::get_singleton();

	// 树结构 diff：无场景固定空串；签名变化 → scene.changed（带完整 tree）。
	// 节流：mutation/undo 后（_tree_dirty_）立即重算；否则 2s 兜底一次（覆盖非 undo 的外部变化），
	// 避免每帧 O(N) 字符串遍历（review P2：大场景帧预算）。
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (_tree_dirty_ || now - _last_tree_check_ms_ > 2000) {
		_last_tree_check_ms_ = now;
		_tree_dirty_ = false;
		const String sig = root ? _tree_signature(root) : String();
		if (sig != _tree_signature_) {
			_tree_signature_ = sig;
			_push_scene_changed();
		}
	}

	// undo/redo 栈 diff：has_undo/has_redo 变化 → editor.undo_stack_changed。
	EditorUndoRedoManager *eurm = EditorUndoRedoManager::get_singleton();
	const bool can_undo = eurm->has_undo();
	const bool can_redo = eurm->has_redo();
	if (can_undo != _can_undo_ || can_redo != _can_redo_) {
		_can_undo_ = can_undo;
		_can_redo_ = can_redo;
		_push_undo_stack_changed();
	}

	if (!root || !ed) {
		return;
	}
	// 选中 Node3D 位置变化：diff 推送（帧轮询）。
	HashMap<ObjectID, Vector3> current;
	List<Node *> nodes = ed->get_editor_selection()->get_full_selected_node_list();
	for (Node *n : nodes) {
		if (Node3D *n3d = Object::cast_to<Node3D>(n)) {
			current[n->get_instance_id()] = n3d->get_position();
		}
	}
	for (const KeyValue<ObjectID, Vector3> &kv : current) {
		const ObjectID id = kv.key;
		if (_tracked_positions_.has(id)) {
			if (_tracked_positions_[id] != kv.value) {
				_tracked_positions_[id] = kv.value; // 更新基线：只发一次变化事件（review P1）
				Dictionary payload;
				payload["node_path"] = String(root->get_path_to(Object::cast_to<Node>(ObjectDB::get_instance(id))));
				Dictionary pos;
				pos["x"] = kv.value.x;
				pos["y"] = kv.value.y;
				pos["z"] = kv.value.z;
				payload["position"] = pos;
				for (Peer &p : peers_) {
					if (p.authenticated && p.peer.is_valid() && !p.dead) {
						Dictionary notify;
						notify["jsonrpc"] = "2.0";
						notify["method"] = "editor.node_position_changed";
						notify["params"] = payload;
						_send(p, notify);
					}
				}
			}
		} else {
			// 新选中节点：记基线（不发初始事件——消费方从 get_state 拉初始值）。
			_tracked_positions_[id] = kv.value;
		}
	}
	// 清掉不再选中的条目。
	Vector<ObjectID> stale;
	for (const KeyValue<ObjectID, Vector3> &kv : _tracked_positions_) {
		if (!current.has(kv.key)) {
			stale.push_back(kv.key);
		}
	}
	for (const ObjectID &id : stale) {
		_tracked_positions_.erase(id);
	}
}

#endif // TOOLS_ENABLED
