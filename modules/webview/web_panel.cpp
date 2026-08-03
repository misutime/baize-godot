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

#include "webview_core.h"
#include "webview_manager.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/main_loop.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

// Windows 平台头经 Godot 头链间接引入 winnt.h:DELETE/PRINT 等是宏,
// 会展开破坏 Key::DELETE / Key::PRINT 枚举引用。局部取消宏定义(本 TU 不用 Windows API)。
#ifdef DELETE
#undef DELETE
#endif
#ifdef PRINT
#undef PRINT
#endif
#ifdef ERROR
#undef ERROR
#endif

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
			// 可点击聚焦:页面点击/键盘输入需要控件持有 Godot 焦点(键盘事件才进 gui_input)。
			set_focus_mode(Control::FOCUS_CLICK);
			// GUI 输入经信号连接(C++ 控件标准模式;_gui_input 非 Control 虚函数)。
			connect(SceneStringName(gui_input), callable_mp(this, &WebPanel::_gui_input));
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
		case NOTIFICATION_FOCUS_ENTER: {
			// 面板获得 Godot 焦点 → 通知 CEF(键盘事件只在有焦点时被 renderer 处理)。
			if (browser_created) {
				WebViewManager::get_singleton()->set_focus(browser_id, true);
			}
			// IME 管道激活不在此处(无条件激活会截获非编辑页面的按键)——由
			// set_focus_editable 回调(editable=true)触发。
		} break;
		case NOTIFICATION_FOCUS_EXIT: {
			if (browser_created) {
				// 焦点离开:取消未完成的 IME 组合(丢弃未提交文本),再通知 CEF 失焦。
				if (ime_composing) {
					WebViewManager::get_singleton()->ime_cancel_composition(browser_id);
					ime_composing = false;
					ime_composing_text.clear();
				}
				WebViewManager::get_singleton()->set_focus(browser_id, false);
			}
			// 释放 IME 管道(ImmAssociateContext 解绑 + DestroyCaret),与 LineEdit 失焦一致。
			_set_ime_active(false);
		} break;
		case NOTIFICATION_WM_WINDOW_FOCUS_IN: {
			// 所属 Window 获得 OS 焦点:IME 更新按 OS 窗口隔离(P2)——窗口级 IME 状态。
			window_has_focus = true;
			if (browser_created && has_focus() && page_focus_editable) {
				_set_ime_active(true);
			}
		} break;
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT: {
			// 窗口失焦:取消组合 + 释放 IME 管道,防止非活动窗口注入组合串(P2)。
			if (browser_created && ime_composing) {
				WebViewManager::get_singleton()->ime_cancel_composition(browser_id);
				ime_composing = false;
				ime_composing_text.clear();
			}
			window_has_focus = false;
			_set_ime_active(false);
		} break;
		case MainLoop::NOTIFICATION_OS_IME_UPDATE: {
			// Godot Windows IME 管道:系统组合文本更新 → 转发 CEF(与 TextEdit 同模式,
			// 见 text_edit.cpp:2151)。组合中 ImeSetComposition,组合结束(文本清空)提交上屏。
			// 仅当本面板所属窗口拥有 OS 焦点时处理(P2:ime_get_text 读的是 OS 焦点窗口的组合)。
			if (!browser_created || !has_focus() || !window_has_focus) {
				break;
			}
			const String new_text = DisplayServer::get_singleton()->ime_get_text();
			if (new_text.is_empty()) {
				if (ime_composing) {
					// 组合结束:Windows IME(微软拼音等)的 GCS_COMPSTR 只含拼音,上屏的汉字以
					// key CHAR 事件到达(实测 unicode=20320 '你' 等)——提交拼音会错误上屏拼音。
					// 正确做法:取消组合(丢弃拼音),上屏汉字由后续 CHAR 事件自然插入。
					WebViewManager::get_singleton()->ime_cancel_composition(browser_id);
					ime_composing = false;
					ime_composing_text.clear();
				}
			} else {
				if (!ime_composing) {
					ime_composing = true;
					// 注意:不在组合开始前 ImeCancelComposition——无活动组合时折叠 caret 不受影响,
					// 但页面有非折叠选区时会提前删除选区(shifu:与 CEF 契约不一致,删掉)。
				}
				const Vector2i sel = DisplayServer::get_singleton()->ime_get_selection();
				ime_composing_text = new_text;
				// Godot ime_get_selection 返回 (组合内光标, 0)——第二个值恒 0,直接传 CefRange(x, y)
				// 会得到反向非法范围 (cursor, 0),CEF 组合节点定位错乱(中文插到文本开头)。
				// 正确语义:0 长度选择在光标处 = CefRange(cursor, cursor)。
				// 注意:Godot 光标是 UTF-32 索引,CEF 按 UTF-16 code unit 解释——前缀转 UTF-16 长度(P2)。
				const int utf16_cursor = new_text.substr(0, sel.x).utf16().length();
				WebViewManager::get_singleton()->ime_set_composition(browser_id, new_text, utf16_cursor, utf16_cursor);
			}
		} break;
		case NOTIFICATION_MOUSE_EXIT: {
			// 鼠标离开面板 → CEF 显式 leave(否则页面收不到 mouseout)。
			if (browser_created) {
				WebViewManager::get_singleton()->send_mouse_move(browser_id, 0, 0, 0, true);
			}
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

void WebPanel::_gui_input(const Ref<InputEvent> &p_event) {
	if (!browser_created) {
		return;
	}
	const uint32_t modifiers = _get_modifiers(p_event);
	const Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid()) {
		const Vector2 pos = motion->get_position();
		WebViewManager::get_singleton()->send_mouse_move(browser_id, static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y), modifiers, false);
		accept_event(); // 已转发:不再传播给 Godot 其他输入路径
		return;
	}
	const Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_valid()) {
		const Vector2 pos = mouse->get_position();
		const int32_t x = static_cast<int32_t>(pos.x);
		const int32_t y = static_cast<int32_t>(pos.y);
		// 高精度滚轮:Windows 后端把原始 delta/WHEEL_DELTA 写入 factor(如 30→0.25,240→2),
		// 直接乘 WHEEL_DELTA 保留真实滚动量;factor 无效时回退 1。
		const float factor = mouse->get_factor() > 0.0f ? mouse->get_factor() : 1.0f;
		const int32_t wheel_delta = static_cast<int32_t>(120 * factor);
		switch (mouse->get_button_index()) {
			case MouseButton::WHEEL_UP:
				WebViewManager::get_singleton()->send_mouse_wheel(browser_id, x, y, modifiers, 0, wheel_delta);
				accept_event();
				return;
			case MouseButton::WHEEL_DOWN:
				WebViewManager::get_singleton()->send_mouse_wheel(browser_id, x, y, modifiers, 0, -wheel_delta);
				accept_event();
				return;
			case MouseButton::WHEEL_LEFT:
				WebViewManager::get_singleton()->send_mouse_wheel(browser_id, x, y, modifiers, wheel_delta, 0);
				accept_event();
				return;
			case MouseButton::WHEEL_RIGHT:
				WebViewManager::get_singleton()->send_mouse_wheel(browser_id, x, y, modifiers, -wheel_delta, 0);
				accept_event();
				return;
			default:
				break;
		}
		// 普通鼠标键:映射 CEF 按钮并转发按下/弹起;按下时抢焦点(点击聚焦)。
		int32_t button = WebViewCore::MOUSE_LEFT;
		switch (mouse->get_button_index()) {
			case MouseButton::LEFT:
				button = WebViewCore::MOUSE_LEFT;
				break;
			case MouseButton::MIDDLE:
				button = WebViewCore::MOUSE_MIDDLE;
				break;
			case MouseButton::RIGHT:
				button = WebViewCore::MOUSE_RIGHT;
				break;
			default:
				return; // 其他按钮(侧键等)不转发,保持向外传播
		}
		if (mouse->is_pressed()) {
			grab_focus(); // 点击面板 → Godot 焦点(键盘事件进 gui_input)
		}
		WebViewManager::get_singleton()->send_mouse_click(browser_id, x, y, modifiers, button, !mouse->is_pressed(), mouse->is_double_click() ? 2 : 1);
		accept_event();
		return;
	}
	const Ref<InputEventKey> key = p_event;
	if (key.is_valid()) {
		// IME 提交合成事件(keycode=NONE + unicode 非空,如"你好" 20320/22909 或 IME 英文模式
		// 提交的 hello):Godot 由孤立 WM_CHAR 合成,physical_keycode 无意义(实测误映射
		// VK_PAUSE)。只发 KEY_CHAR 且 windows_key_code=unicode——CEF 151 Windows OSR 的
		// CHAR 路径用 windows_key_code 作字符载荷(FromCharacter),传 VK 会变错字/控制符。
		if (key->is_pressed() && key->get_keycode() == Key::NONE && key->get_unicode() != 0) {
			const uint32_t unicode = key->get_unicode();
			WebViewManager::get_singleton()->send_key_event(browser_id, WebViewCore::KEY_CHAR, modifiers, static_cast<int>(unicode), 0, unicode, unicode, false);
			// IME 提交完成:清除组合状态(Windows WM_IME_ENDCOMPOSITION 不发 IME_UPDATE,
			// 不在此清会导致状态滞留、后续可打印键被组合抑制逻辑丢弃——P2)。
			if (ime_composing) {
				ime_composing = false;
				ime_composing_text.clear();
			}
			accept_event();
			return;
		}
		// IME 组合中:上屏由 ime_commit_text 提交,CHAR 事件会双插——组合中抑制字符转发
		// (候选键/方向键的 RAWKEYDOWN 照常转发,供候选选择)。
		if (ime_composing && key->get_unicode() != 0) {
			return;
		}
		// 物理键码做 VK 映射(Shift+1 的物理键是 KEY_1;逻辑 keycode 对 Shift 标点如 EXCLAM
		// 无对应 VK,且 Godot 标点码≠Windows VK——见 _key_to_windows_vk 的 OEM 映射)。
		const int vk = _key_to_windows_vk(key->get_physical_keycode());
		if (vk == 0) {
			return; // 未映射键(修饰键本身/未知):不转发,保持向外传播
		}
		uint32_t modifiers = _get_modifiers(p_event);
		if (key->is_echo()) {
			modifiers |= WebViewCore::MOD_IS_REPEAT; // CEF 重复按键标志
		}
		const Key phys = key->get_physical_keycode();
		if (phys >= Key::KP_0 && phys <= Key::KP_9) {
			modifiers |= WebViewCore::MOD_IS_KEY_PAD; // 小键盘事件标志
		}
		const uint32_t unicode = key->get_unicode();
		// 无修饰字符(CEF 快捷键判定):Alt/Ctrl 组合会改变字符(AltGr 布局),无修饰值取物理键
		// 的基础 ASCII;否则(Shift 保留)直接用 unicode。
		uint32_t unmodified = unicode;
		if ((key->is_alt_pressed() || key->is_ctrl_pressed()) && vk >= 0x20 && vk <= 0x7E) {
			unmodified = static_cast<uint32_t>(vk);
		}
		if (key->is_pressed()) {
			WebViewManager::get_singleton()->send_key_event(browser_id, WebViewCore::KEY_RAWKEYDOWN, modifiers, vk, 0, 0, 0, false);
			if (unicode != 0) {
				// CHAR 的 windows_key_code 必须传 unicode(不是 vk):CEF 151 Windows OSR 的 CHAR
				// 路径用 windows_key_code 作字符载荷(FromCharacter),传物理 VK('d'键=68='D')
				// 会导致输入恒为大写/shifu 源码级确认。character 同时填 unicode(其他平台语义)。
				WebViewManager::get_singleton()->send_key_event(browser_id, WebViewCore::KEY_CHAR, modifiers, static_cast<int>(unicode), 0, unicode, unmodified, false);
			}
		} else {
			WebViewManager::get_singleton()->send_key_event(browser_id, WebViewCore::KEY_KEYUP, modifiers, vk, 0, 0, 0, false);
		}
		accept_event();
	}
}

uint32_t WebPanel::_get_modifiers(const Ref<InputEvent> &p_event) {
	uint32_t mods = 0;
	const Ref<InputEventWithModifiers> with_mods = p_event;
	if (with_mods.is_valid()) {
		if (with_mods->is_shift_pressed()) {
			mods |= WebViewCore::MOD_SHIFT;
		}
		if (with_mods->is_ctrl_pressed()) {
			mods |= WebViewCore::MOD_CONTROL;
		}
		if (with_mods->is_alt_pressed()) {
			mods |= WebViewCore::MOD_ALT;
		}
		if (with_mods->is_meta_pressed()) {
			mods |= WebViewCore::MOD_COMMAND;
		}
	}
	// 鼠标按钮按下状态:仅鼠标事件携带(InputEventMouse::get_button_mask);
	// 移动时其他按钮可能仍按住,CEF modifiers 的 mouse 位表示"当前按下"。
	const Ref<InputEventMouse> mouse_ev = p_event;
	if (mouse_ev.is_valid()) {
		const BitField<MouseButtonMask> mask = mouse_ev->get_button_mask();
		if (mask.has_flag(MouseButtonMask::LEFT)) {
			mods |= WebViewCore::MOD_LEFT_MOUSE;
		}
		if (mask.has_flag(MouseButtonMask::MIDDLE)) {
			mods |= WebViewCore::MOD_MIDDLE_MOUSE;
		}
		if (mask.has_flag(MouseButtonMask::RIGHT)) {
			mods |= WebViewCore::MOD_RIGHT_MOUSE;
		}
	}
	return mods;
}

void WebPanel::set_focus_editable(bool p_focus_on_editable) {
	page_focus_editable = p_focus_on_editable;
	if (!browser_created) {
		return;
	}
	if (p_focus_on_editable && has_focus() && window_has_focus) {
		// 页面焦点进入可编辑元素 → 激活 IME 管道(ImmAssociateContext + CreateCaret),
		// 否则中文输入法组合(WM_IME_*)不工作;非编辑节点不激活(P1:截获按键回归)。
		_set_ime_active(true);
	} else if (!p_focus_on_editable) {
		// 页面焦点离开可编辑元素:取消组合 + 释放 IME 管道。
		if (ime_composing) {
			WebViewManager::get_singleton()->ime_cancel_composition(browser_id);
			ime_composing = false;
			ime_composing_text.clear();
		}
		_set_ime_active(false);
	}
}

void WebPanel::_set_ime_active(bool p_active) {
	DisplayServerEnums::WindowID wid = get_window() ? get_window()->get_window_id() : DisplayServerEnums::INVALID_WINDOW_ID;
	if (wid == DisplayServerEnums::INVALID_WINDOW_ID || !DisplayServer::get_singleton()->has_feature(DisplayServerEnums::FEATURE_IME)) {
		return;
	}
	if (p_active) {
		DisplayServer::get_singleton()->window_set_ime_position(get_global_position(), wid); // 基础版:候选窗定位到面板
		DisplayServer::get_singleton()->window_set_ime_active(true, wid);
	} else {
		DisplayServer::get_singleton()->window_set_ime_active(false, wid);
	}
}

int WebPanel::_key_to_windows_vk(Key p_key) {
	// 直通区:空格(0x20)、数字 0-9(0x30-0x39)、A-Z(0x41-0x5A)——Godot Key 码 == Windows VK。
	// 注意:此处输入的是 physical_keycode(Shift+1 的物理键是 KEY_1,逻辑 keycode 对 Shift
	// 标点如 EXCLAM 无对应 VK);其余 ASCII 标点 Godot 码≠VK(如 APOSTROPHE 0x27 是 VK_RIGHT),
	// 必须走下方 OEM 映射(reviewer P1: 标点直通会发错键码)。
	if (p_key == Key::SPACE || (p_key >= Key::KEY_0 && p_key <= Key::KEY_9) || (p_key >= Key::A && p_key <= Key::Z)) {
		return static_cast<int>(p_key);
	}
	switch (p_key) {
		// ---- ASCII 标点 → VK_OEM_* (Windows 布局相关虚拟键) ----
		case Key::SEMICOLON:
			return 0xBA; // VK_OEM_1 (;:)
		case Key::EQUAL:
			return 0xBB; // VK_OEM_PLUS (=+)
		case Key::COMMA:
			return 0xBC; // VK_OEM_COMMA (,<)
		case Key::MINUS:
			return 0xBD; // VK_OEM_MINUS (-_)
		case Key::PERIOD:
			return 0xBE; // VK_OEM_PERIOD (.>)
		case Key::SLASH:
			return 0xBF; // VK_OEM_2 (/?)
		case Key::QUOTELEFT:
			return 0xC0; // VK_OEM_3 (`~)
		case Key::BRACKETLEFT:
			return 0xDB; // VK_OEM_4 ([{)
		case Key::BACKSLASH:
			return 0xDC; // VK_OEM_5 (\|)
		case Key::BRACKETRIGHT:
			return 0xDD; // VK_OEM_6 (]})
		case Key::APOSTROPHE:
			return 0xDE; // VK_OEM_7 ('")
		// ---- 特殊键 ----
		case Key::ESCAPE:
			return 0x1B; // VK_ESCAPE
		case Key::TAB:
			return 0x09; // VK_TAB
		case Key::BACKSPACE:
			return 0x08; // VK_BACK
		case Key::ENTER:
		case Key::KP_ENTER:
			return 0x0D; // VK_RETURN
		case Key::INSERT:
			return 0x2D; // VK_INSERT
		case Key::KEY_DELETE:
			return 0x2E; // VK_DELETE
		case Key::HOME:
			return 0x24; // VK_HOME
		case Key::END:
			return 0x23; // VK_END
		case Key::LEFT:
			return 0x25; // VK_LEFT
		case Key::UP:
			return 0x26; // VK_UP
		case Key::RIGHT:
			return 0x27; // VK_RIGHT
		case Key::DOWN:
			return 0x28; // VK_DOWN
		case Key::PAGEUP:
			return 0x21; // VK_PRIOR
		case Key::PAGEDOWN:
			return 0x22; // VK_NEXT
		case Key::PAUSE:
			return 0x13; // VK_PAUSE
		case Key::PRINT:
			return 0x2C; // VK_SNAPSHOT
		case Key::CAPSLOCK:
			return 0x14; // VK_CAPITAL
		case Key::NUMLOCK:
			return 0x90; // VK_NUMLOCK
		case Key::SCROLLLOCK:
			return 0x91; // VK_SCROLL
		// ---- 数字小键盘(VK_NUMPAD0-9 = 0x60-0x69;调用方附加 MOD_IS_KEY_PAD) ----
		case Key::KP_0:
			return 0x60;
		case Key::KP_1:
			return 0x61;
		case Key::KP_2:
			return 0x62;
		case Key::KP_3:
			return 0x63;
		case Key::KP_4:
			return 0x64;
		case Key::KP_5:
			return 0x65;
		case Key::KP_6:
			return 0x66;
		case Key::KP_7:
			return 0x67;
		case Key::KP_8:
			return 0x68;
		case Key::KP_9:
			return 0x69;
		case Key::KP_MULTIPLY:
			return 0x6A; // VK_MULTIPLY (*)
		case Key::KP_ADD:
			return 0x6B; // VK_ADD (+)
		case Key::KP_SUBTRACT:
			return 0x6D; // VK_SUBTRACT (-)
		case Key::KP_PERIOD:
			return 0x6E; // VK_DECIMAL (.)
		case Key::KP_DIVIDE:
			return 0x6F; // VK_DIVIDE (/)
		default:
			break;
	}
	// 功能键 F1-F24(VK_F1=0x70 起)。
	if (p_key >= Key::F1 && p_key <= Key::F24) {
		return 0x70 + (static_cast<int>(p_key) - static_cast<int>(Key::F1));
	}
	return 0; // 未映射(修饰键本身/未知):调用方跳过
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
