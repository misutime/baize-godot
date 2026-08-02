/**************************************************************************/
/*  webview_manager.cpp                                                   */
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

#include "webview_manager.h"

#include "web_panel.h"

#include "core/extension/gdextension_manager.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"

WebViewManager *WebViewManager::singleton = nullptr;

WebViewManager *WebViewManager::get_singleton() {
	if (!singleton) {
		singleton = memnew(WebViewManager);
	}
	return singleton;
}

void WebViewManager::free_singleton() {
	memdelete(singleton);
	singleton = nullptr;
}

void WebViewManager::init_core() {
	if (core) {
		return;
	}
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	WvCallbacks cbs;
	cbs.on_paint = &WebViewManager::_on_paint;
	cbs.on_message = nullptr; // M2 接入 IPC
	cbs.on_load_status = &WebViewManager::_on_load_status;
	core = wv_create(exe_dir.utf8().get_data(), &cbs, nullptr);
	if (!core) {
		ERR_PRINT("[WebView] Rust core create failed.");
		return;
	}
	print_line("[WebView] Rust core created (4A M1b).");
}

void WebViewManager::shutdown_core() {
	if (core) {
		wv_destroy(core);
		core = nullptr;
	}
}

void WebViewManager::pump() {
	if (core) {
		wv_pump(core);
	}
}

void WebViewManager::register_panel(WebPanel *p_panel) {
	int32_t id = next_browser_id++;
	p_panel->set_browser_id(id);
	panels[id] = p_panel;
}

void WebViewManager::unregister_panel(int32_t p_id) {
	panels.erase(p_id);
}

int WebViewManager::create_browser(int32_t p_id, const String &p_url, int32_t p_w, int32_t p_h) {
	if (!core) {
		ERR_PRINT("[WebView] create_browser before core ready.");
		return -1;
	}
	CharString url = p_url.utf8();
	return wv_create_browser(core, p_id, url.get_data(), p_w, p_h);
}

void WebViewManager::resize_browser(int32_t p_id, int32_t p_w, int32_t p_h) {
	if (core) {
		wv_resize_browser(core, p_id, p_w, p_h);
	}
}

void WebViewManager::destroy_browser(int32_t p_id) {
	if (core) {
		wv_destroy_browser(core, p_id);
	}
}

void WebViewManager::navigate_browser(int32_t p_id, const String &p_url) {
	if (core) {
		CharString url = p_url.utf8();
		wv_navigate_browser(core, p_id, url.get_data());
	}
}

void WebViewManager::_on_paint(void *p_userdata, int32_t p_id, const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h) {
	WebViewManager *mgr = get_singleton();
	WebPanel **slot = mgr->panels.getptr(p_id);
	if (slot && *slot) {
		(*slot)->set_paint(p_rgba, p_w, p_h);
	}
}

void WebViewManager::_on_load_status(void *p_userdata, int32_t p_id, int32_t p_status, const char *p_url) {
	print_line("[WebView] load status: id=" + itos(p_id) + " status=" + itos(p_status));
}
