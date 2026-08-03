// SPDX-License-Identifier: MIT

#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

#include <cstdint>

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

private:
	// ---- 方法实现（按命名空间分组；p_args_json 为参数对象 JSON，含 req_id）----
	static void _method_scene_get_node_count(int32_t p_browser_id, const String &p_args_json);
	static void _method_scene_create_node(int32_t p_browser_id, const String &p_args_json);
	static void _method_editor_undo(int32_t p_browser_id, const String &p_args_json);
	static void _method_editor_redo(int32_t p_browser_id, const String &p_args_json);

	/// 应答下行："method_result" 事件携带 { req_id, ok, result } / { req_id, ok:false, error }。
	static void _respond(int32_t p_browser_id, const String &p_req_id, bool p_ok, const Variant &p_result,
			const String &p_error_code = "", const String &p_error_message = "");
};
