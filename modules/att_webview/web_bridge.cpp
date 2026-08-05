// SPDX-License-Identifier: MIT

#include "web_bridge.h"

#include "webview_manager.h"

#include "modules/att_editor_ops/ops.h"
#include "modules/att_editor_ops/registry.h"

#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "core/string/print_string.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/settings/editor_settings.h"
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
	} else if (p_method == "editor.get_ui_font_size") {
		_method_editor_get_ui_font_size(p_browser_id, args_json);
	} else if (p_method == "editor.get_ui_scale") {
		_method_editor_get_ui_scale(p_browser_id, args_json);
	} else if (p_method == "editor.get_ui_font") {
		_method_editor_get_ui_font(p_browser_id, args_json);
	} else if (p_method == "editor.get_ui_font_bold") {
		_method_editor_get_ui_font_bold(p_browser_id, args_json);
	} else {
		_respond(p_browser_id, req_id, false, Variant(), "method_not_found", "未注册的方法: " + p_method);
	}
}

void WebBridge::_method_scene_get_node_count(int32_t p_browser_id, const String &p_args_json) {
	// 委托 Registry（editor_ops 能力面唯一事实源）。
	_dispatch_semantic(p_browser_id, "scene.get_node_count", p_args_json);
}

void WebBridge::_method_scene_create_node(int32_t p_browser_id, const String &p_args_json) {
	// 委托 Registry（能力面唯一事实源）；返回形状适配在 _dispatch_semantic 内完成。
	_dispatch_semantic(p_browser_id, "scene.create_node", p_args_json);
}

void WebBridge::_method_scene_get_node_position(int32_t p_browser_id, const String &p_args_json) {
	// 能力合流（2026-08-05，迁移至 att_editor_ops）：委托 Registry，协议适配在 _dispatch_semantic。
	_dispatch_semantic(p_browser_id, "scene.get_node_position", p_args_json);
}
void WebBridge::_method_scene_set_node_position(int32_t p_browser_id, const String &p_args_json) {
	// 能力合流（2026-08-05，迁移至 att_editor_ops）：委托 Registry，协议适配在 _dispatch_semantic。
	_dispatch_semantic(p_browser_id, "scene.set_node_position", p_args_json);
}

void WebBridge::_method_editor_get_ui_font_size(int32_t p_browser_id, const String &p_args_json) {
	// 能力合流（2026-08-05，迁移至 att_editor_ops）：委托 Registry，协议适配在 _dispatch_semantic。
	_dispatch_semantic(p_browser_id, "editor.get_ui_font_size", p_args_json);
}
void WebBridge::_method_editor_get_ui_scale(int32_t p_browser_id, const String &p_args_json) {
	// 能力合流（2026-08-05，迁移至 att_editor_ops）：委托 Registry，协议适配在 _dispatch_semantic。
	_dispatch_semantic(p_browser_id, "editor.get_ui_scale", p_args_json);
}
void WebBridge::_method_editor_get_ui_font(int32_t p_browser_id, const String &p_args_json) {
	// 能力合流（2026-08-05，迁移至 att_editor_ops）：委托 Registry，协议适配在 _dispatch_semantic。
	_dispatch_semantic(p_browser_id, "editor.get_ui_font", p_args_json);
}
void WebBridge::_method_editor_get_ui_font_bold(int32_t p_browser_id, const String &p_args_json) {
	// 能力合流（2026-08-05，迁移至 att_editor_ops）：委托 Registry，协议适配在 _dispatch_semantic。
	_dispatch_semantic(p_browser_id, "editor.get_ui_font_bold", p_args_json);
}
void WebBridge::_method_editor_undo(int32_t p_browser_id, const String &p_args_json) {
	// 委托 Registry（能力面唯一事实源）；Ops::undo 与旧实现同为
	// EditorUndoRedoManager::undo()，错误码 nothing_to_undo 保持一致。
	_dispatch_semantic(p_browser_id, "editor.undo", p_args_json);
}

void WebBridge::_method_editor_redo(int32_t p_browser_id, const String &p_args_json) {
	// 委托 Registry（能力面唯一事实源）；Ops::redo 与旧实现同为
	// EditorUndoRedoManager::redo()，错误码 nothing_to_redo 保持一致。
	_dispatch_semantic(p_browser_id, "editor.redo", p_args_json);
}

void WebBridge::_dispatch_semantic(int32_t p_browser_id, const String &p_method, const String &p_args_json) {
	// 能力面合流（方案 §5.2 S1）：语义方法（scene.* / editor.*）统一委托 Registry
	// （editor_ops 能力面唯一事实源），WebBridge 仅保留协议适配层。
	// 参数: { req_id, ...方法参数 }。req_id 仅用于应答配对，其余字段原样交给注册表校验/执行
	// （校验只查 required 存在性，handler 用 get() 取参——多余 key 无副作用）。
	String req_id;
	const Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		req_id = parsed.operator Dictionary().get("req_id", "").operator String();
	}
	const Registry::Method *method = Registry::find(p_method);
	if (!method) {
		_respond(p_browser_id, req_id, false, Variant(), "method_not_found", "未注册的方法: " + p_method);
		return;
	}
	Dictionary args;
	String verr;
	if (!Registry::validate_args(*method, parsed, args, verr)) {
		_respond(p_browser_id, req_id, false, Variant(), "invalid_params", verr);
		return;
	}
	const Dictionary result = method->handler(args);
	if (result.get("ok", false).operator bool()) {
		Variant payload = result.get("result", Variant());
		if (p_method == "scene.create_node") {
			// 返回形状适配：SDK createNode 期望裸 node_id（instance_id，number，与
			// node_position_changed 事件的 node_id 同语义）；注册表 handler 返回
			// { instance_id, path, name } 三元组——取 instance_id 转发，其余字段丢弃。
			payload = payload.operator Dictionary().get("instance_id", Variant());
		}
		_respond(p_browser_id, req_id, true, payload);
		return;
	}
	const Dictionary error = result.get("error", Dictionary()).operator Dictionary();
	_respond(p_browser_id, req_id, false, Variant(), error.get("code", "").operator String(), error.get("message", "").operator String());
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
String WebBridge::last_scene_key_; // 空 = 首帧必发一次当前场景状态
int WebBridge::last_ui_font_size_ = -1; // 哨兵：首帧比较必发当前值（连接时即设基线，实际不发）
String WebBridge::last_ui_font_; // main_font 路径基线


void WebBridge::set_event_browser_id(int32_t p_browser_id) {
	event_browser_id_ = p_browser_id;
	tracked_positions_.clear(); // 目标切换：清空基线，防旧节点缓存误发
	// 重置 undo 哨兵：新消费者首帧 poll 收到当前栈状态（否则旧值 diff 吞掉状态）。
	last_can_undo_ = true;
	last_can_redo_ = true;
	last_scene_key_ = String(); // 场景基线重置：新消费者首帧 poll 收到当前场景状态
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
	// 编辑器设置变化（任何设置变化都触发，回调内过滤 main_font_size）。
	EditorSettings::get_singleton()->connect("settings_changed", callable_mp_static(&WebBridge::_on_editor_settings_changed));
	event_sources_connected_ = true;
	// 基线：当前字体大小/路径（不在此发初始事件——页面经 get_ui_font_size/get_ui_font 拉取）。
	last_ui_font_size_ = EditorSettings::get_singleton()->get_setting("interface/editor/fonts/main_font_size");
	last_ui_font_ = EditorSettings::get_singleton()->get_setting("interface/editor/fonts/main_font");
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
	if (EditorSettings::get_singleton()) {
		EditorSettings::get_singleton()->disconnect("settings_changed", callable_mp_static(&WebBridge::_on_editor_settings_changed));
	}
	event_sources_connected_ = false;
}

void WebBridge::poll_editor_state() {
	if (event_browser_id_ < 0) {
		return; // 无事件下行目标（浏览器未就绪/已注销）
	}
	_refresh_tracked_positions(false); // 纯 diff：位置变化才发
	_poll_undo_stack();
	_poll_scene_state();
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

void WebBridge::_on_editor_settings_changed() {
	if (event_browser_id_ < 0) {
		return;
	}
	EditorSettings *es = EditorSettings::get_singleton();
	const int size = es->get_setting("interface/editor/fonts/main_font_size");
	if (size != last_ui_font_size_) {
		last_ui_font_size_ = size;
		Dictionary body;
		body["size"] = size;
		emit_event(event_browser_id_, "editor.ui_font_size_changed", JSON::stringify(body));
	}
	const String font = es->get_setting("interface/editor/fonts/main_font");
	if (font != last_ui_font_) {
		last_ui_font_ = font;
		Dictionary body;
		// 实际生效路径（与 get_ui_font 一致：设置优先，空则 resolved 默认字体）。
		body["path"] = font.is_empty() ? Ops::get_resolved_main_font() : font;
		emit_event(event_browser_id_, "editor.ui_font_changed", JSON::stringify(body));
	}
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

void WebBridge::_poll_scene_state() {
	// 场景上下文（有无根 + 路径）变化时下行 scene_changed。轮询 diff 而非信号：
	// EditorNode::scene_changed 不覆盖当前标签内新建/撤销根节点（SceneTreeDock
	// add_root_node → set_edited_scene 路径不发射该信号）——轮询与 node_position/
	// undo 同机制，统一覆盖打开/关闭/切换标签/建根/删根全部路径。
	EditorInterface *ei = EditorInterface::get_singleton();
	Node *root = ei ? ei->get_edited_scene_root() : nullptr;
	const String key = String::num_int64(root != nullptr ? 1 : 0) + "|" + (root ? root->get_scene_file_path() : String());
	if (key == last_scene_key_) {
		return;
	}
	last_scene_key_ = key;
	Dictionary body;
	body["has_scene"] = root != nullptr;
	body["scene_path"] = root ? root->get_scene_file_path() : String();
	emit_event(event_browser_id_, "editor.scene_changed", JSON::stringify(body));
}
