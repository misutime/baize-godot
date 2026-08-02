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

#include <cstring>

WebPanel::~WebPanel() = default;

void WebPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_url", "url"), &WebPanel::set_url);
	ClassDB::bind_method(D_METHOD("get_url"), &WebPanel::get_url);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");
}

void WebPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			texture_rect = memnew(TextureRect);
			texture_rect->set_anchors_preset(Control::PRESET_FULL_RECT);
			texture_rect->set_stretch_mode(TextureRect::STRETCH_SCALE);
			add_child(texture_rect);
			set_process(true); // 驱动 Rust 核心消息泵
			// 注册面板，获取 browser_id；立即尝试按当前尺寸创建浏览器
			// （注意：NOTIFICATION_RESIZED 可能在 _ready 之前触发，届时 id 未分配，见 sync_size 守卫）。
			WebViewManager::get_singleton()->register_panel(this);
			sync_size();
		} break;
		case NOTIFICATION_PROCESS: {
			WebViewManager::get_singleton()->pump();
		} break;
		case NOTIFICATION_RESIZED: {
			sync_size();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (browser_created) {
				WebViewManager::get_singleton()->destroy_browser(browser_id);
				browser_created = false;
			}
			if (browser_id >= 0) {
				WebViewManager::get_singleton()->unregister_panel(browser_id);
				browser_id = -1;
			}
		} break;
		default:
			break;
	}
}

void WebPanel::set_url(const String &p_url) {
	url = p_url;
	if (browser_created) {
		// 已建浏览器：直接导航到新 URL（而非只缓存字符串）。
		WebViewManager::get_singleton()->navigate_browser(browser_id, url);
	}
}

void WebPanel::set_paint(const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h) {
	if (p_w == 0 || p_h == 0 || !p_rgba) {
		return;
	}
	// 每次 paint 重建 Image + ImageTexture（软件渲染 60fps 可接受；GPU 路径 V2 替换）。
	PackedByteArray data;
	data.resize(p_w * p_h * 4);
	memcpy(data.ptrw(), p_rgba, p_w * p_h * 4);
	Ref<Image> image = Image::create_from_data(p_w, p_h, false, Image::FORMAT_RGBA8, data);
	if (image.is_null()) {
		return;
	}
	texture = ImageTexture::create_from_image(image);
	if (texture_rect) {
		texture_rect->set_texture(texture);
	}
}

void WebPanel::sync_size() {
	// RESIZED 可能早于 _ready（注册分配 id）触发——未注册时不创建，等 READY 主动同步。
	if (browser_id < 0) {
		return;
	}
	Size2i size = get_size();
	if (size.x <= 0 || size.y <= 0) {
		return;
	}
	if (browser_created) {
		WebViewManager::get_singleton()->resize_browser(browser_id, size.x, size.y);
	} else {
		int ret = WebViewManager::get_singleton()->create_browser(browser_id, url, size.x, size.y);
		if (ret == 0) {
			browser_created = true;
			print_line("[WebView] WebPanel browser created: id=" + itos(browser_id) + " url=" + url);
		} else {
			ERR_PRINT("[WebView] WebPanel browser create failed: id=" + itos(browser_id));
		}
	}
}
