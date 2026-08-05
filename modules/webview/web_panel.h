/**************************************************************************/
/*  web_panel.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                   */
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

#include <cstdint>

// 编辑器网页面板（C++ 路线，窗口模式/非 OSR）：CEF 在编辑器主窗口内创建原生子窗口，
// 面板负责把自身矩形（窗口逻辑坐标 × 内容缩放 = 物理像素）同步到子窗口，并把 Godot
// CanvasItem 可见性同步到子窗口显隐。像素零回传，输入/IME 由 CEF 原生窗口直接接收。
// 坐标约束：仅支持宿主主窗口根视口（无 canvas transform / CanvasLayer 层级的编辑器
// dock 场景）——其他层级下 get_global_rect 不含视口变换，子窗口会错位。
// 生命周期：NOTIFICATION_ENTER_TREE 注册面板 + 创建浏览器（每次入树触发——dock 移动 =
// 出树再入树，READY 仅首次触发不可用），NOTIFICATION_PROCESS 每帧同步子窗口矩形与可见性，
// NOTIFICATION_EXIT_TREE 销毁浏览器并注销面板。消息泵由 WebViewManager 单例挂
// SceneTree::process_frame 驱动。
//   url: String          设置加载地址（已建浏览器时直接导航）
//   send_message(json)   向页面发送消息（IPC 通路 M2 接入，当前仅日志）
//   on_message(信号)     页面消息到达
class WebPanel : public Control {
	GDCLASS(WebPanel, Control);

	int32_t browser_id = -1;
	String url;
	bool browser_created = false;
	Rect2i last_phys_rect_ = Rect2i(); // 已下发给 CEF 的物理矩形（变化才 MoveWindow；浏览器重建后须复位为哨兵）
	bool last_visible_ = true; // 最近一次同步给 CEF 的可见性（变化才 ShowWindow）

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	/// 计算面板在宿主窗口内的物理像素矩形（逻辑坐标 × 窗口内容缩放）并同步到 CEF 子窗口。
	void _sync_native_bounds();

public:
	~WebPanel();

	void set_url(const String &p_url);
	String get_url() const;

	int32_t get_browser_id() const { return browser_id; }
	void set_browser_id(int32_t p_id) { browser_id = p_id; }

	/// 面板尺寸变化或首次布局后调用（首次创建浏览器；之后矩形同步走 _sync_native_bounds）。
	void sync_size();

	/// 向页面发送消息：M2 接入 IPC（respond_query）后实现，当前打印日志。
	void send_message(const String &p_msg);

	// 页面可观测性（WebViewManager 静态回调分发到面板）。
	void _on_ipc_message(const String &p_msg);
	void _on_load_finished(const String &p_url, int p_http_status);
	void _on_load_error(const String &p_url, int p_error_code, const String &p_error_text);
};
