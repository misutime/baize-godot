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

// 编辑器网页面板（4A）：经 C ABI 驱动 Rust 核心的 OSR 浏览器，
// 软件渲染 paint（RGBA）→ ImageTexture → TextureRect 显示。
class WebPanel : public Control {
	GDCLASS(WebPanel, Control);

	int32_t browser_id = -1;
	String url;
	bool browser_created = false;

	TextureRect *texture_rect = nullptr;
	Ref<ImageTexture> texture;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	~WebPanel();

	void set_url(const String &p_url);
	String get_url() const { return url; }

	int32_t get_browser_id() const { return browser_id; }
	void set_browser_id(int32_t p_id) { browser_id = p_id; }

	/// 由 WebViewManager 的 paint 回调调用（主线程，paint 期间缓冲有效）。
	void set_paint(const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h);

	/// 面板尺寸变化或首次布局后调用（创建/调整浏览器）。
	void sync_size();
};
