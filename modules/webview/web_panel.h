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

#include "core/input/input_event.h"
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

	Ref<ImageTexture> texture; // OSR 纹理（_draw 直接绘制，无子控件布局依赖）
	// paint 缓存:尺寸不变时复用 Image/ImageTexture(update 上传),避免每帧重建;
	// paint_buffer 为每帧 memcpy 目标(避免 Vector 反复分配)。
	Ref<Image> paint_image;
	Vector<uint8_t> paint_buffer;
	uint32_t paint_width = 0;
	uint32_t paint_height = 0;
	// IME 组合状态:组合中(ime_composing)抑制 CHAR 转发防双插;结束提交 ime_composing_text。
	bool ime_composing = false;
	String ime_composing_text;
	// 页面焦点是否在可编辑元素(focusedEditableNodeChanged 回调):IME 管道激活依据——
	// 非编辑节点聚焦时激活会截获按键(P1)。
	bool page_focus_editable = false;
	// 所属 Window 是否拥有 OS 焦点(WM_WINDOW_FOCUS_IN/OUT):IME 更新按 OS 窗口隔离(P2)。
	bool window_has_focus = false;
	// resize 拖动节流：拖动中每帧 RESIZED 都 resize 会让 CEF 渲染队列积压（软件渲染
	// 异步，纹理尺寸严重滞后面板 → 被 STRETCH_SCALE 拉伸变形）。节流窗口内合并，
	// 窗口过期由 NOTIFICATION_PROCESS 补发最新目标尺寸（停止拖动后仍收敛）。
	// 25ms ≈ 40fps 更新：比 50ms 平滑（拖动态时更跟手），仍远大于单次渲染耗时防积压。
	static constexpr int RESIZE_THROTTLE_MS = 25;
	uint64_t last_resize_ms_ = 0;
	Size2i pending_size_ = Size2i(-1, -1); // 最新目标尺寸（节流窗口内每帧更新）
	Size2i applied_size_ = Size2i(-1, -1); // 已下发给 CEF 的尺寸
	Size2i last_paint_size_ = Size2i(-1, -1); // CEF 最近一次 OnPaint 输出的纹理尺寸

protected:
	static void _bind_methods();
	void _notification(int p_what);
	/// GUI 输入处理(经 gui_input 信号连接,NOTIFICATION_READY 中建立)。
	void _gui_input(const Ref<InputEvent> &p_event);

private:
	/// 输入事件 → CEF 修饰键位标志(webview_core.h MOD_*)；含鼠标按钮按下状态。
	static uint32_t _get_modifiers(const Ref<InputEvent> &p_event);
	/// Godot Key → Windows 虚拟键码(VK)；未映射返回 0(调用方跳过转发)。ASCII 区直接透传。
	static int _key_to_windows_vk(Key p_key);
	/// 激活/释放 IME 管道(window_set_ime_active + 候选窗位置;仅 FEATURE_IME 支持时)。
	void _set_ime_active(bool p_active);

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

	/// 页面可编辑焦点回调（WebViewManager 分发）：editable=true 时激活 IME 管道（若窗口有焦点）。
	void set_focus_editable(bool p_focus_on_editable);

	/// 向页面发送消息：M2 接入 IPC（respond_query）后实现，当前打印日志。
	void send_message(const String &p_msg);

	// 页面可观测性（WebViewManager 静态回调分发到面板）。
	void _on_ipc_message(const String &p_msg);
	void _on_load_finished(const String &p_url, int p_http_status);
	void _on_load_error(const String &p_url, int p_error_code, const String &p_error_text);
};
