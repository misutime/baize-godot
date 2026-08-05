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

#include "webview_manager.h"

#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

WebPanel::~WebPanel() = default;

void WebPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_url", "url"), &WebPanel::set_url);
	ClassDB::bind_method(D_METHOD("get_url"), &WebPanel::get_url);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");

	ClassDB::bind_method(D_METHOD("send_message", "message"), &WebPanel::send_message);
	ClassDB::bind_method(D_METHOD("_on_ipc_message", "message"), &WebPanel::_on_ipc_message);

	ADD_SIGNAL(MethodInfo("on_message", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("load_finished")); // 页面加载完成（事件源初始快照等订阅时机）
}

void WebPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// 每次入树都注册面板并创建浏览器——不能用 NOTIFICATION_READY（_ready() 仅首次入树
			// 触发一次）：dock 移动 = remove_child + add_child（editor_dock.cpp _move_dock
			// :360/:385），面板出树再入树时 READY 不重触发，浏览器销毁后不再重建（实测：
			// 移动 webdock 到右侧/与其他 dock 合并后内部空白）。
			// 消息泵由 WebViewManager 单例挂 SceneTree::process_frame 每帧驱动（与面板数量解耦）。
			WebViewManager::get_singleton()->register_panel(this);
			sync_size();
			set_process(true); // 每帧同步子窗口矩形（dock 拖动/布局变化即时跟随）
		} break;
		case NOTIFICATION_RESIZED: {
			sync_size();
		} break;
		case NOTIFICATION_PROCESS: {
			// 窗口模式：每帧把面板矩形（物理像素）同步到 CEF 子窗口（MoveWindow）。
			_sync_native_bounds();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (browser_created) {
				WebViewManager::get_singleton()->destroy_browser(browser_id);
				browser_created = false;
			}
			last_phys_rect_ = Rect2i(); // 失效矩形缓存:同面板重新入树时新浏览器须重新全量同步
			if (browser_id >= 0) {
				WebViewManager::get_singleton()->unregister_panel(browser_id);
				browser_id = -1;
			}
		} break;
		default:
			break;
	}
}

void WebPanel::_sync_native_bounds() {
	if (!browser_created) {
		return;
	}
	Window *win = get_window();
	if (win == nullptr) {
		return;
	}
	// 可见性同步:Godot CanvasItem 可见性不传播到 OS 子窗口——面板隐藏(dock 折叠/切页/
	// 布局隐藏)时子窗口必须显式隐藏,否则盖住其他编辑器内容;重新可见时强制重下发矩形。
	const bool visible = is_visible_in_tree();
	if (visible != last_visible_) {
		last_visible_ = visible;
		WebViewManager::get_singleton()->set_browser_visible(browser_id, visible);
		if (visible) {
			last_phys_rect_ = Rect2i(); // 哨兵:隐藏期间布局可能已变化,强制下次全量同步
		}
	}
	if (!visible) {
		return; // 隐藏期间不同步矩形
	}
	const DisplayServerEnums::WindowID win_id = win->get_window_id();
	// 坐标约束:仅支持宿主主窗口根视口(无 canvas transform / CanvasLayer 层级)——WebDock
	// 专用场景。get_global_rect 不含 Viewport canvas/popup 与 CanvasLayer final transform,
	// 其他层级下子窗口会错位(创建时已对嵌入视口告警,见 sync_size)。
	const float scale = DisplayServer::get_singleton()->window_get_scale(win_id);
	const Rect2 global = get_global_rect();
	const Rect2i phys(
			Math::round(global.position.x * scale),
			Math::round(global.position.y * scale),
			Math::round(global.size.x * scale),
			Math::round(global.size.y * scale));
	if (phys.size.x <= 0 || phys.size.y <= 0) {
		return;
	}
	if (phys != last_phys_rect_) {
		last_phys_rect_ = phys;
		WebViewManager::get_singleton()->resize_browser(browser_id, phys.position.x, phys.position.y, phys.size.x, phys.size.y);
	}
}

void WebPanel::sync_size() {
	// RESIZED 可能早于 READY（注册分配 id）触发——未注册时不创建，等 READY 主动同步。
	if (browser_id < 0) {
		return;
	}
	const Size2i size = get_size();
	if (size.x <= 0 || size.y <= 0) {
		return;
	}
	if (browser_created) {
		// 已创建：矩形同步由 NOTIFICATION_PROCESS 的 _sync_native_bounds 每帧完成。
		return;
	}
	Window *win = get_window();
	if (win == nullptr) {
		return;
	}
	// 坐标约束防御:仅支持主窗口根视口(WebDock 专用场景)。嵌入视口(SubViewport/
	// CanvasLayer transform)下 get_global_rect 不含视口变换,原生子窗口会错位——
	// 显式告警而非静默(见 _sync_native_bounds 坐标约束注释)。
	if (Viewport *vp = get_viewport(); vp != nullptr && vp->get_parent_viewport() != nullptr) {
		WARN_PRINT("[WebView] WebPanel inside embedded viewport: native child window coordinates unsupported");
	}
	// 宿主窗口原生句柄：Windows = 编辑器主窗口 HWND；mac = 内容 NSView（未实机验证）。
	void *parent_handle = nullptr;
	const DisplayServerEnums::WindowID win_id = win->get_window_id();
#if defined(_WIN32)
	parent_handle = reinterpret_cast<void *>(DisplayServer::get_singleton()->window_get_native_handle(DisplayServerEnums::WINDOW_HANDLE, win_id));
#elif defined(__APPLE__)
	parent_handle = reinterpret_cast<void *>(DisplayServer::get_singleton()->window_get_native_handle(DisplayServerEnums::WINDOW_VIEW, win_id));
#endif
	if (parent_handle == nullptr) {
		ERR_PRINT("[WebView] WebPanel parent native handle unavailable: id=" + itos(browser_id));
		return;
	}
	const float scale = DisplayServer::get_singleton()->window_get_scale(win_id);
	const int phys_w = Math::round(size.x * scale);
	const int phys_h = Math::round(size.y * scale);
	const int ret = WebViewManager::get_singleton()->create_browser(browser_id, url, phys_w, phys_h, parent_handle);
	if (ret == 0) {
		browser_created = true;
		last_phys_rect_ = Rect2i(); // 哨兵:新浏览器至少完整下发一次矩形(重建后旧缓存失效)
		last_visible_ = is_visible_in_tree();
		print_line("[WebView] WebPanel browser created: id=" + itos(browser_id) + " url=" + url);
		_sync_native_bounds(); // 立即按当前矩形放置（创建时初始位置为 0,0）
	} else {
		ERR_PRINT("[WebView] WebPanel browser create failed: id=" + itos(browser_id));
	}
}

void WebPanel::set_url(const String &p_url) {
	url = p_url;
	if (browser_created) {
		// 已建浏览器：直接导航到新 URL（而非只缓存字符串）。
		WebViewManager::get_singleton()->navigate_browser(browser_id, url);
	}
}

String WebPanel::get_url() const {
	return url;
}

void WebPanel::send_message(const String &p_msg) {
	// M2：JS 查询应答经 WebViewManager::respond_query 下发（需 on_query 侧维护 pending 查询）；
	// 当前 IPC 通路未接，仅打印日志保持 API 契约。
	print_line("[WebView] send_message (M2 pending, not delivered): " + p_msg);
}

void WebPanel::_on_ipc_message(const String &p_msg) {
	emit_signal(SNAME("on_message"), p_msg);
}

void WebPanel::_on_load_finished(const String &p_url, int p_http_status) {
	print_line("[WebView] page loaded: " + p_url + " (status " + itos(p_http_status) + ")");
	emit_signal(SNAME("load_finished")); // 订阅方（WebDockPlugin）在此时机下发初始状态
}

void WebPanel::_on_load_error(const String &p_url, int p_error_code, const String &p_error_text) {
	ERR_PRINT("[WebView] page load error " + itos(p_error_code) + ": " + p_error_text + " (" + p_url + ")");
}
