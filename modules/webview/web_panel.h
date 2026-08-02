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
#include "scene/gui/texture_rect.h"

#include "core/io/image.h"
#include "scene/resources/image_texture.h"

#include <cstdint>

// 编辑器网页面板（C++ 路线）：经 WebViewManager 驱动 WebViewCore（C++ 核心，封装 CEF）
// 的 OSR 浏览器，软件渲染 paint（RGBA）→ ImageTexture → TextureRect 显示。
// 生命周期：NOTIFICATION_READY 注册面板 + 同步尺寸（消息泵由 WebViewManager 单例挂
// SceneTree::process_frame 驱动，面板不参与），NOTIFICATION_RESIZED 同步尺寸，
// NOTIFICATION_EXIT_TREE 销毁浏览器并注销面板。
//   url: String          设置加载地址（已建浏览器时直接导航）
//   send_message(json)   向页面发送消息（IPC 通路 M2 接入，当前仅日志）
//   on_message(信号)     页面消息到达
class WebPanel : public Control {
	GDCLASS(WebPanel, Control);

	int32_t browser_id = -1;
	String url;
	bool browser_created = false;

	TextureRect *texture_rect = nullptr;
	Ref<ImageTexture> texture;
	// paint 缓存:尺寸不变时复用 Image/ImageTexture(update 上传),避免每帧重建;
	// paint_buffer 为每帧 memcpy 目标(避免 Vector 反复分配)。
	Ref<Image> paint_image;
	Vector<uint8_t> paint_buffer;
	uint32_t paint_width = 0;
	uint32_t paint_height = 0;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	~WebPanel();

	void set_url(const String &p_url);
	String get_url() const;

	int32_t get_browser_id() const { return browser_id; }
	void set_browser_id(int32_t p_id) { browser_id = p_id; }

	/// 由 WebViewManager 的 paint 回调调用（主线程，paint 期间缓冲有效，内部拷贝）。
	void set_paint(const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h);

	/// 面板尺寸变化或首次布局后调用（创建/调整浏览器）。
	void sync_size();

	/// 向页面发送消息：M2 接入 IPC（respond_query）后实现，当前打印日志。
	void send_message(const String &p_msg);

	// 页面可观测性（WebViewManager 静态回调分发到面板）。
	void _on_ipc_message(const String &p_msg);
	void _on_load_finished(const String &p_url, int p_http_status);
	void _on_load_error(const String &p_url, int p_error_code, const String &p_error_text);
};
