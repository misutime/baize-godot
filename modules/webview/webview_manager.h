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

#include "webview_ffi.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

class WebPanel;

// 引擎级 webview 管理单例（4A）。
// 职责：持有 Rust 核心（WebViewCore）生命周期、每帧 pump（WebPanel._process 驱动）、
// 面板注册表（browser_id → WebPanel）与 C ABI 回调分发。
class WebViewManager {
	static WebViewManager *singleton;

	WebViewCore *core = nullptr;
	HashMap<int32_t, WebPanel *> panels;
	int32_t next_browser_id = 0;

public:
	static WebViewManager *get_singleton();
	static void free_singleton();

	void init_core();
	void shutdown_core();
	void pump();

	void register_panel(WebPanel *p_panel);
	void unregister_panel(int32_t p_id);

	int create_browser(int32_t p_id, const String &p_url, int32_t p_w, int32_t p_h);
	void resize_browser(int32_t p_id, int32_t p_w, int32_t p_h);
	void destroy_browser(int32_t p_id);
	void navigate_browser(int32_t p_id, const String &p_url);

	// C ABI 回调（Rust 核心 → C++ 壳）。
	static void _on_paint(void *p_userdata, int32_t p_id, const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h);
	static void _on_load_status(void *p_userdata, int32_t p_id, int32_t p_status, const char *p_url);
};
