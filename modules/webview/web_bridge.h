// SPDX-License-Identifier: MIT

#pragma once

#include "core/math/vector3.h"
#include "core/object/object_id.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

#include <cstdint>

class Node3D; // 仅指针引用（实现位于 web_bridge.cpp）

// 桥协议方法注册表（Godot 壳层）。协议规范见
// 《doc/plans/Godot编辑器UI重构方案-TS路线-WebUI架构-桥协议与前端SDK.md》。
//
// 职责：
// - 接收 WebViewCore 的 invokeMethod 上行（经 WebViewManager 静态回调），按方法名分派
//   到编辑器逻辑（scene.* / editor.* 命名空间）；
// - 处理后经 emit_event 下行 "method_result" 应答（req_id 配对，SDK 封装成 Promise）；
// - 编辑器事件源（selection_changed 等）经 emit_event 下行。
//
// 线程：全部在主线程（与 pump 同线程）。参数约定：对象参数由前端 SDK JSON.stringify
// 成字符串传入，本层 JSON 解析。
class WebBridge {
public:
	/// 处理一次 invoke 上行。p_method 为点号命名空间方法名；p_args 为字符串化参数
	/// （协议约定 args[0] = 前端 SDK 序列化的参数对象 JSON，含 req_id）。
	static void handle_invoke(int32_t p_browser_id, const String &p_method, const Vector<String> &p_args);

	/// 触发事件下行到指定浏览器（协议层封装；p_payload_json 为事件 payload JSON）。
	static void emit_event(int32_t p_browser_id, const String &p_event_name, const String &p_payload_json);

	// ---- 事件源（MVP2 后半；机制见《WebUI架构-桥协议与前端SDK.md》§6）----

	/// 注册事件下行目标浏览器（-1 = 注销）。WebDock 面板浏览器就绪后由 WebDockPlugin
	/// 注册；事件源只发往该目标。现状：单 WebDock 单浏览器（多面板时改注册表）。
	static void set_event_browser_id(int32_t p_browser_id);

	/// 初始化编辑器事件源（幂等）：连接 EditorSelection::selection_changed。
	/// 接收方为静态方法（callable_mp_static），无对象悬空风险；EditorSelection 随
	/// EditorNode 销毁时连接自动清理，deinit 仅用于 EditorNode 存活期的显式断开。
	static void init_event_sources();
	/// 断开事件源连接（EditorNode 已销毁时安全空操作）。
	static void deinit_event_sources();

	/// 帧轮询编辑器状态（WebDockPlugin NOTIFICATION_PROCESS 驱动）：
	/// node_position_changed（选中 Node3D 位置 diff，阈值 1e-6）+ undo_stack_changed。
	static void poll_editor_state();

	/// 页面加载完成（订阅就绪）后下发完整初始状态：当前选中 node_paths + 各选中
	/// 节点初始位置 + 下帧 undo 栈状态。选中先于浏览器就绪时事件会被跳过，必须在此
	/// 强制快照（WebDockPlugin 在 WebPanel::load_finished 信号时调用）。
	static void emit_initial_state();

private:
	// ---- 方法实现（按命名空间分组；p_args_json 为参数对象 JSON，含 req_id）----
	static void _method_scene_get_node_count(int32_t p_browser_id, const String &p_args_json);
	static void _method_scene_create_node(int32_t p_browser_id, const String &p_args_json);
	static void _method_scene_get_node_position(int32_t p_browser_id, const String &p_args_json);
	static void _method_scene_set_node_position(int32_t p_browser_id, const String &p_args_json);
	static void _method_editor_undo(int32_t p_browser_id, const String &p_args_json);
	static void _method_editor_redo(int32_t p_browser_id, const String &p_args_json);

	/// 场景相对路径 → Node3D 公共解析：空路径/无场景/节点缺失或非 Node3D 时发出
	/// 对应错误应答（invalid_params / no_scene / invalid_node）并返回 nullptr。
	static Node3D *_resolve_node3d(int32_t p_browser_id, const String &p_req_id, const String &p_node_path);

	/// 应答下行："method_result" 事件携带 { req_id, ok, result } / { req_id, ok:false, error }。
	static void _respond(int32_t p_browser_id, const String &p_req_id, bool p_ok, const Variant &p_result,
			const String &p_error_code = "", const String &p_error_message = "");

	// ---- 事件源内部 ----
	static int32_t event_browser_id_; // 事件下行目标（-1 未注册）
	static bool event_sources_connected_; // selection_changed 连接标志（幂等）
	// 选中 Node3D 位置基线（帧轮询 diff；仅 Node3D，MVP 验收语义）。
	static HashMap<ObjectID, Vector3> tracked_positions_;
	static bool last_can_undo_; // undo 栈上次状态（哨兵 true：首帧 diff 必发一次）
	static bool last_can_redo_;

	/// EditorSelection::selection_changed → 下行 selection_changed(node_paths) +
	/// 重建位置跟踪基线（新选中节点立即发初始位置，验收 2：选中即显示 X）。
	static void _on_selection_changed();
	/// 刷新选中 Node3D 位置跟踪；p_emit_initial_for_new 时对新选中节点立即发初始位置。
	static void _refresh_tracked_positions(bool p_emit_initial_for_new);
	static void _emit_node_position_changed(ObjectID p_node_id, const Vector3 &p_position);
	static void _poll_undo_stack();
};
