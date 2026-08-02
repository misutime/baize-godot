/**************************************************************************/
/*  web_panel.h                                                           */
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

#include "scene/gui/control.h"

// 编辑器网页面板（Route B 统一 API，对应《设计》§3.3 WebPanel）。
// 内部封装 gdcef 的 CefTexture（GDExtension 类，经 ClassDB/Object API 交互）：
//   url: String          设置加载地址
//   send_message(json)   向页面发送消息
//   on_message(信号)     页面消息到达
class WebPanel : public Control {
	GDCLASS(WebPanel, Control);

	Object *cef_object = nullptr; // CefTexture 实例（GDExtension 类，无 C++ 绑定，经 Object API 调用）
	String url;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	~WebPanel();

	void set_url(const String &p_url);
	String get_url() const;

	void send_message(const String &p_msg);
	void _on_ipc_message(const String &p_msg);
	void _on_load_finished(const String &p_url, int p_http_status);
	void _on_load_error(const String &p_url, int p_error_code, const String &p_error_text);

	void _ensure_cef();
};
