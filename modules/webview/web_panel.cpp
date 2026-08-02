/**************************************************************************/
/*  web_panel.cpp                                                         */
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

#include "web_panel.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"

WebPanel::~WebPanel() {
	// cef_object 是 GDExtension 对象，随本 Control 从场景树移除后由引擎引用计数释放；
	// 这里不手动释放（GDExtension 对象生命周期归 ClassDB 引用管理）。
}

void WebPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_url", "url"), &WebPanel::set_url);
	ClassDB::bind_method(D_METHOD("get_url"), &WebPanel::get_url);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");

	ClassDB::bind_method(D_METHOD("send_message", "message"), &WebPanel::send_message);
	ClassDB::bind_method(D_METHOD("_on_ipc_message", "message"), &WebPanel::_on_ipc_message);

	ADD_SIGNAL(MethodInfo("on_message", PropertyInfo(Variant::STRING, "message")));
}

void WebPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_ensure_cef();
			set_url(url);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// CefTexture 随场景树移除，置空指针避免悬挂。
			cef_object = nullptr;
		} break;
		default:
			break;
	}
}

void WebPanel::_ensure_cef() {
	if (cef_object) {
		return;
	}
	// 必须用 instantiate_no_placeholders：CefTexture 是非 tool 类（gdext 默认 ToolClassesOnly），
	// 编辑器进程里普通 instantiate 会返回占位对象（通知 no-op）→ CEF 永不初始化、页面空白。
	cef_object = ClassDB::instantiate_no_placeholders(SNAME("CefTexture"));
	if (!cef_object) {
		ERR_PRINT("[WebView] CefTexture class not found — gdcef extension not loaded?");
		return;
	}
	Control *cef_control = Object::cast_to<Control>(cef_object);
	if (!cef_control) {
		ERR_PRINT("[WebView] CefTexture is not a Control.");
		cef_object = nullptr;
		return;
	}
	cef_control->set_anchors_preset(Control::PRESET_FULL_RECT);
	add_child(cef_control);
	cef_object->connect(SNAME("ipc_message"), callable_mp(this, &WebPanel::_on_ipc_message));
	// 页面加载的可观测性：状态/错误直接打日志，避免静默空白。
	cef_object->connect(SNAME("load_finished"), callable_mp(this, &WebPanel::_on_load_finished));
	cef_object->connect(SNAME("load_error"), callable_mp(this, &WebPanel::_on_load_error));
}

void WebPanel::_on_load_finished(const String &p_url, int p_http_status) {
	print_line("[WebView] page loaded: " + p_url + " (status " + itos(p_http_status) + ")");
}

void WebPanel::_on_load_error(const String &p_url, int p_error_code, const String &p_error_text) {
	ERR_PRINT("[WebView] page load error " + itos(p_error_code) + ": " + p_error_text + " (" + p_url + ")");
}

void WebPanel::set_url(const String &p_url) {
	url = p_url;
	if (cef_object) {
		cef_object->set(SNAME("url"), url);
	}
}

String WebPanel::get_url() const {
	return url;
}

void WebPanel::send_message(const String &p_msg) {
	ERR_FAIL_COND_MSG(!cef_object, "[WebView] send_message before CefTexture ready.");
	cef_object->call(SNAME("send_ipc_message"), p_msg);
}

void WebPanel::_on_ipc_message(const String &p_msg) {
	emit_signal(SNAME("on_message"), p_msg);
}
