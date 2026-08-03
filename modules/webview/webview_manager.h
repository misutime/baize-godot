/**************************************************************************/
/*  webview_manager.h                                                     */
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

#pragma once

#include "webview_core.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

#include <cstdint>

class Object;
class WebPanel;

// 引擎级 webview 管理单例（C++ 路线）。
// 职责：持有 WebViewCore（C++ 核心，封装 CEF）生命周期、每帧 pump（单例驱动：
// init_core 成功后挂 SceneTree::process_frame，与面板数量解耦，最后面板退出后
// 异步关闭送达前仍持续泵送）、面板注册表（browser_id → WebPanel）与核心回调分发
// （on_paint / on_load_status / on_query）。
// 不再加载 gdcef 扩展：CEF 由核心层直接初始化（init_core 惰性，失败为终态）。
class WebViewManager {
	static WebViewManager *singleton;

	WebViewCore core_; // C++ 核心（非 Godot 对象，值成员，随单例生命周期）
	bool core_initialized_ = false; // init 尝试过一次即置位（失败为终态，不重试）
	HashMap<int32_t, WebPanel *> panels_;
	int32_t next_browser_id_ = 0;
	Object *pump_driver_ = nullptr; // SceneTree::process_frame 连接目标（每帧 pump；具体类型在 .cpp 匿名命名空间）

	void start_frame_pump(); // init_core 成功后挂载（幂等）
	void stop_frame_pump(); // free_singleton 时卸载（幂等）

public:
	static WebViewManager *get_singleton(); // 惰性创建（外部调用）
	static WebViewManager *peek_singleton(); // 可空读取（静态回调用，不创建，防 teardown 后复活）
	static void free_singleton();

	void init_core(); // 惰性：首次 create_browser 前调用（幂等；失败为终态，不重试）
	void shutdown_core(); // 幂等；free_singleton 内部先调用（关闭全部浏览器 + CefShutdown）
	void pump(); // 每帧一次，由 pump_driver_（SceneTree::process_frame）驱动（核心层内部节流）

	void register_panel(WebPanel *p_panel);
	void unregister_panel(int32_t p_id);

	int create_browser(int32_t p_id, const String &p_url, int32_t p_w, int32_t p_h);
	void resize_browser(int32_t p_id, int32_t p_w, int32_t p_h);
	void destroy_browser(int32_t p_id);
	void navigate_browser(int32_t p_id, const String &p_url);

	// JS 查询应答（on_query 回调给出的 p_query_id）。M2 接入 IPC 后由 WebPanel::send_message 使用。
	bool respond_query(int32_t p_id, int64_t p_query_id, bool p_success, const String &p_response, int p_error);

	// 输入事件转发（OSR：面板 GUI 输入 → WebViewCore → CEF）。
	// 参数语义与 WebViewCore 对应 API 一致（见 webview_core.h）。
	void send_mouse_move(int32_t p_id, int32_t p_x, int32_t p_y, uint32_t p_modifiers, bool p_leave);
	void send_mouse_click(int32_t p_id, int32_t p_x, int32_t p_y, uint32_t p_modifiers, int32_t p_button, bool p_up, int32_t p_click_count);
	void send_mouse_wheel(int32_t p_id, int32_t p_x, int32_t p_y, uint32_t p_modifiers, int32_t p_delta_x, int32_t p_delta_y);
	void send_key_event(int32_t p_id, int32_t p_type, uint32_t p_modifiers, int32_t p_windows_key_code, int32_t p_native_key_code, uint32_t p_character, uint32_t p_unmodified_character, bool p_focus_on_editable);
	void set_focus(int32_t p_id, bool p_focus);

	// WebViewCore 回调（主线程，pump 内同步触发）→ 静态分发到面板注册表。
	static void _on_paint(int32_t p_id, const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h);
	static void _on_load_status(int32_t p_id, int32_t p_status, const std::string &p_url);
	static void _on_query(int32_t p_id, const std::string &p_query, int64_t p_query_id);
};
