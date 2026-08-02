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

#include <climits>
#include <cstring>

WebPanel::~WebPanel() = default;

void WebPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_url", "url"), &WebPanel::set_url);
	ClassDB::bind_method(D_METHOD("get_url"), &WebPanel::get_url);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");

	ClassDB::bind_method(D_METHOD("send_message", "message"), &WebPanel::send_message);
	ClassDB::bind_method(D_METHOD("_on_ipc_message", "message"), &WebPanel::_on_ipc_message);

	ADD_SIGNAL(MethodInfo("on_message", PropertyInfo(Variant::STRING, "message")));
}

void WebPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// 显示纹理：OSR paint 经 Image → ImageTexture 到此 TextureRect。
			texture_rect = memnew(TextureRect);
			texture_rect->set_anchors_preset(Control::PRESET_FULL_RECT);
			texture_rect->set_stretch_mode(TextureRect::STRETCH_SCALE);
			add_child(texture_rect);
			// 注册面板分配 browser_id；立即按当前尺寸创建浏览器。
			// 注意：NOTIFICATION_RESIZED 可能早于 READY 触发，届时 id 未分配，见 sync_size 守卫。
			// 消息泵不再由面板驱动：WebViewManager 在 init_core 成功后挂 SceneTree::process_frame
			// 每帧恰好泵一次（与面板数量解耦，最后面板退出后仍持续泵到异步关闭送达）。
			WebViewManager::get_singleton()->register_panel(this);
			sync_size();
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

String WebPanel::get_url() const {
	return url;
}

void WebPanel::set_paint(const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h) {
	if (p_w == 0 || p_h == 0 || !p_rgba) {
		return;
	}
	// checked 乘法:按 size_t(64 位)计算,拒绝超过 Vector 容量(int)的尺寸——
	// 防 4K/60fps 下 uint32 乘法溢出导致负长度 resize 或截断拷贝。
	const size_t byte_count = static_cast<size_t>(p_w) * static_cast<size_t>(p_h) * 4;
	if (byte_count > static_cast<size_t>(INT_MAX)) {
		ERR_PRINT("[WebView] set_paint: buffer too large (" + itos(p_w) + "x" + itos(p_h) + ")");
		return;
	}
	if (paint_image.is_null() || paint_width != p_w || paint_height != p_h) {
		// 尺寸变化才重建 Image + ImageTexture(尺寸/格式变化必须重建)。
		paint_width = p_w;
		paint_height = p_h;
		paint_buffer.resize(static_cast<int>(byte_count));
		memcpy(paint_buffer.ptrw(), p_rgba, byte_count);
		paint_image = Image::create_from_data(p_w, p_h, false, Image::FORMAT_RGBA8, paint_buffer);
		if (paint_image.is_null()) {
			return;
		}
		texture = ImageTexture::create_from_image(paint_image);
	} else {
		// 尺寸不变:复用 Image(拷贝到已有缓冲后 set_data 覆盖)与 ImageTexture(update 上传),
		// 避免每帧重建 Vector/Image/ImageTexture 的分配压力。
		memcpy(paint_buffer.ptrw(), p_rgba, byte_count);
		paint_image->set_data(p_w, p_h, false, Image::FORMAT_RGBA8, paint_buffer);
		texture->update(paint_image);
	}
	if (texture_rect) {
		texture_rect->set_texture(texture);
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
		WebViewManager::get_singleton()->resize_browser(browser_id, size.x, size.y);
	} else {
		const int ret = WebViewManager::get_singleton()->create_browser(browser_id, url, size.x, size.y);
		if (ret == 0) {
			browser_created = true;
			print_line("[WebView] WebPanel browser created: id=" + itos(browser_id) + " url=" + url);
		} else {
			ERR_PRINT("[WebView] WebPanel browser create failed: id=" + itos(browser_id));
		}
	}
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
}

void WebPanel::_on_load_error(const String &p_url, int p_error_code, const String &p_error_text) {
	ERR_PRINT("[WebView] page load error " + itos(p_error_code) + ": " + p_error_text + " (" + p_url + ")");
}
