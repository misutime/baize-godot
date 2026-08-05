/**************************************************************************/
/*  editor_web_dock.h                                                     */
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

#ifdef TOOLS_ENABLED

#include "editor/docks/editor_dock.h"
#include "editor/plugins/editor_plugin.h"
#include "modules/webview/web_panel.h"

// 编辑器 WebDock 插件：创建 EditorDock 承载 WebPanel（DOCK_SLOT_LEFT_UL，可拖拽停靠）。
// 生命周期经 _notification（本 fork 的 Node 生命周期 hook 为 GDCLASS 分发链，
// _notification 非虚函数，不能加 override 关键字）。
class WebDockPlugin : public EditorPlugin {
	GDCLASS(WebDockPlugin, EditorPlugin);

	WebPanel *web_panel = nullptr;
	EditorDock *web_dock = nullptr;
	// 事件源下行目标是否已注册到 WebBridge（面板 browser_id 就绪后置位，防重复注册）。
	bool event_target_registered_ = false;

protected:
	void _notification(int p_what);
	/// 页面加载完成（WebPanel::load_finished）：下发事件源初始状态快照。
	void _on_panel_load_finished();

public:
	// 延迟注册入口（MessageQueue 第一帧执行；此时 EditorNode 已由 Main::start 创建）。
	void register_dock();
};

// 自由函数形式的延迟入口（callable_mp_static 需要）。
void register_web_dock_deferred();

#endif // TOOLS_ENABLED
