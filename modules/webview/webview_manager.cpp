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
	// M0：创建 Rust 核心句柄；回调（paint/message/load）在 M1 接入。
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	core = wv_create(exe_dir.utf8().get_data(), nullptr, nullptr);
	if (!core) {
		ERR_PRINT("[WebView] Rust core create failed.");
		return;
	}
	print_line("[WebView] Rust core created (4A M0).");
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

void WebViewManager::load_cef_extension() {
	// 分发目录约定：<exe_dir>/webview/（开发态 = bin/webview/，由 `just webview-stage` 暂存）。
	const String ext_path = OS::get_singleton()->get_executable_path().get_base_dir()
									.path_join("webview")
									.path_join("godot_cef.gdextension");

	if (!FileAccess::exists(ext_path)) {
		// 未暂存 = 模块惰性状态，但不静默：打印可观测提示。
		print_line("[WebView] CEF extension not staged at " + ext_path + " — run `just webview-stage` first.");
		return;
	}

	print_line("[WebView] Loading CEF extension: " + ext_path);
	const GDExtensionManager::LoadStatus status = GDExtensionManager::get_singleton()->load_extension(ext_path);
	switch (status) {
		case GDExtensionManager::LOAD_STATUS_OK:
			print_line("[WebView] CEF extension loaded OK.");
			break;
		case GDExtensionManager::LOAD_STATUS_ALREADY_LOADED:
			print_line("[WebView] CEF extension already loaded.");
			break;
		case GDExtensionManager::LOAD_STATUS_NEEDS_RESTART:
			print_line("[WebView] CEF extension load requires restart (minimum level mismatch).");
			break;
		default:
			ERR_PRINT("[WebView] CEF extension load FAILED (status " + itos(status) + ").");
			break;
	}
}
