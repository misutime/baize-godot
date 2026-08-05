/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "editor_web_dock.h"
#include "web_panel.h"
#include "webview_manager.h"

#if defined(__APPLE__)
#include "cef_application_mac.h" // 注入 CEF mac 事件集成所需方法（须在 CEF 调用前）
#endif

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/message_queue.h"

void initialize_webview_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(WebPanel);
#if defined(__APPLE__)
		// mac：CEF 消息泵依赖 NSApplication 的 isHandlingSendEvent/setHandlingSendEvent
		// （CrAppProtocol），GodotApplication 未实现——这里在首次 CEF 调用前注入。
		webview_install_cef_application_protocol();
#endif
		// C++ 路线：不再加载 gdcef 扩展；单例持有 WebViewCore，CEF 在首次
		// create_browser（WebPanel::sync_size）时经 init_core 惰性初始化。
		WebViewManager::get_singleton();
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		// EditorNode 在 Main::start() 创建（晚于模块初始化），deferred 到第一帧注册 dock。
		// 插件作为 EditorNode 子节点，随编辑器退出自动释放（不单独 unregister）。
		MessageQueue::get_singleton()->push_callable(callable_mp_static(register_web_dock_deferred));
	}
#endif
}

void uninitialize_webview_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// free_singleton 内部先 shutdown_core（关闭全部浏览器 + CefShutdown），再释放单例。
		WebViewManager::free_singleton();
	}
}
