/**************************************************************************/
/*  editor_web_dock.cpp                                                   */
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

#include "editor_web_dock.h"

#ifdef TOOLS_ENABLED

#include "editor/editor_node.h"

#include "core/os/os.h"

// 编辑器自带页面：<exe_dir>/webview/ui/，经 file:// 加载——与打开的项目无关。
static String get_bundled_ui_url() {
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	const String file_path = exe_dir.path_join("webview").path_join("ui").path_join("bridge.html");
	return "file:///" + file_path.replace("\\", "/");
}

void WebDockPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			web_panel = memnew(WebPanel);
			web_panel->set_name(SNAME("WebDock"));
			// MarginContainer 内必须展开填充，否则收缩为 0x0 → CEF 纹理无尺寸不渲染。
			web_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			web_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			web_panel->set_custom_minimum_size(Size2(320, 240));

			web_dock = memnew(EditorDock);
			web_dock->set_title("WebDock");
			web_dock->set_default_slot(EditorDock::DOCK_SLOT_LEFT_UL);
			web_dock->set_closable(true);
			web_dock->add_child(web_panel);

			web_panel->set_url(get_bundled_ui_url());
			add_dock(web_dock);
			print_line("[WebView] WebDock registered (LEFT_UL), url=" + get_bundled_ui_url());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (web_dock) {
				// 编辑器退出拆除阶段 EditorNode 可能已销毁——守卫后再触碰 dock 系统。
				if (EditorNode::get_singleton()) {
					remove_dock(web_dock);
				}
				web_dock->queue_free(); // web_panel 是 web_dock 的子节点，随之释放
				web_dock = nullptr;
				web_panel = nullptr;
			}
		} break;
		default:
			break;
	}
}

void WebDockPlugin::register_dock() {
	// 经 EditorNode::add_editor_plugin 注册；插件成为 EditorNode 子节点，
	// ENTER_TREE 通知触发 _notification 建 dock。
	EditorNode::add_editor_plugin(this);
}

void register_web_dock_deferred() {
	if (EditorNode::get_singleton()) {
		memnew(WebDockPlugin)->register_dock();
	} else {
		// EditorNode 应已在 Main::start() 创建；此处不静默——失败可见。
		ERR_PRINT("[WebView] EditorNode not ready when registering WebDock.");
	}
}

#endif // TOOLS_ENABLED
