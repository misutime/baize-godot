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

void WebViewManager::load_cef_extension_if_requested() {
	const String path = OS::get_singleton()->get_environment("GODOT_CEF_EXTENSION");
	if (path.is_empty()) {
		// 未设置环境变量 = 模块惰性状态，但不静默：打印一行可观测日志。
		print_line("[WebView] CEF extension load skipped (GODOT_CEF_EXTENSION not set).");
		return;
	}

	print_line("[WebView] Loading CEF extension: " + path);
	const GDExtensionManager::LoadStatus status = GDExtensionManager::get_singleton()->load_extension(path);
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
