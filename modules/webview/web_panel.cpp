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
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

// GPU OSR（mac）：CEF OnAcceleratedPaint 交付 IOSurface → Metal 打开 → 回调内同步
// blit（commit + waitUntilCompleted）到自有 Metal 纹理 → texture_create_from_extension
// 导入 RD 供直接采样。RenderingDevice 接口已具备跨 API 纹理导入
// （texture_create_from_extension，见 texture_storage.cpp:1415 注释），无需引擎改动。
#if defined(__APPLE__) && defined(RD_ENABLED) && defined(METAL_ENABLED)
#include "drivers/metal/rendering_context_driver_metal.h"
#include "servers/rendering/rendering_device.h"
#include <Metal/Metal.hpp>
#endif

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
	ADD_SIGNAL(MethodInfo("load_finished")); // 页面加载完成（事件源初始快照等订阅时机）
}

void WebPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
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
			set_process(true); // 节流补发：拖动停止后把最后一次 pending 尺寸下发给 CEF
		} break;
		case NOTIFICATION_RESIZED: {
			sync_size();
		} break;
		case NOTIFICATION_DRAW: {
			// 直接绘制 OSR 纹理：纹理与面板一致时精确全幅；不一致（resize 未收敛的
			// 滞后窗口）时 1:1 左上裁剪——不变形（stretch 压字）。余区/首次 OnPaint
			// 前不填充：露出面板原生底色——问题暴露时黑边是可视诊断信号（用户定案）。
			// 绘制区域 = WebPanel 自身 rect（position 0,0、尺寸 = get_size()），
			// 无子控件布局依赖。
			// 纹理源：GPU OSR（gpu_path_active）优先绘制自有 RD 纹理（无 CPU 读回），
			// 否则软件 ImageTexture（回退路径）。两路径共用同一裁剪策略。
			Ref<Texture2D> tex;
			if (gpu_path_active && gpu_texture.is_valid()) {
				tex = gpu_texture;
			} else if (texture.is_valid()) {
				tex = texture;
			} else {
				return;
			}
			const Size2 panel = get_size();
			const Size2 tex_size = tex->get_size();
			if (tex_size == panel) {
				draw_texture_rect(tex, Rect2(Point2(), panel), false);
			} else {
				const Size2 draw_size(Size2(MIN(tex_size.x, panel.x), MIN(tex_size.y, panel.y)));
				if (draw_size.x > 0 && draw_size.y > 0) {
					draw_texture_rect_region(tex, Rect2(Point2(), draw_size), Rect2(Point2(), draw_size));
				}
			}
		} break;
		case NOTIFICATION_PROCESS: {
			const uint64_t now_ms = OS::get_singleton()->get_ticks_msec();
			// 分支 1（新 desired 下发）：pending 变更（pending != applied）且未渲染出
			// → 节流窗口后下发最新 desired。区分“新目标”与“同尺寸重发”（shifu 终审：
			// 原条件 pending != last_paint 在停止后未收敛时每 25ms 都满足，分支 2 成死代码）。
			if (browser_created && pending_size_ != applied_size_ &&
					pending_size_ != last_paint_size_ &&
					now_ms - last_resize_ms_ >= static_cast<uint64_t>(RESIZE_THROTTLE_MS)) {
				last_resize_ms_ = now_ms;
				applied_size_ = pending_size_;
				WebViewManager::get_singleton()->resize_browser(browser_id, pending_size_.x, pending_size_.y);
			} else if (browser_created && applied_size_ != last_paint_size_ &&
					now_ms - last_resize_ms_ > 250) {
				// 分支 2（尾随重发）：无新 desired 但 applied 未渲染出 → 250ms 后重发
				// 同尺寸 WasResized+Invalidate，强制合成器活动加速收敛。不改 GetViewBounds
				// 为其他尺寸（避免 CEF hold 死锁：旧 surface 永远 ≠ 新期望）。
				// 250ms 折中：比原 1s 快（收敛速度接近当前 25ms 高频验证效果），
				// 比 25ms 低频（减少对 CEF hold 的无效刺激）。
				last_resize_ms_ = now_ms;
				WebViewManager::get_singleton()->resize_browser(browser_id, applied_size_.x, applied_size_.y);
			}
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
			_free_gpu_texture(); // GPU OSR 自有纹理（先解包 RS 再 free RD RID）
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
	// 软件帧到达：交付切回软件路径（与 GPU 直通互斥的防御性处理——CEF 按
	// shared_texture_enabled 二选一交付，正常不会两路同发）。
	gpu_path_active = false;
	// checked 乘法:按 size_t(64 位)计算,拒绝超过 Vector 容量(int)的尺寸——
	// 防 4K/60fps 下 uint32 乘法溢出导致负长度 resize 或截断拷贝。
	const size_t byte_count = static_cast<size_t>(p_w) * static_cast<size_t>(p_h) * 4;
	if (byte_count > static_cast<size_t>(INT_MAX)) {
		ERR_PRINT("[WebView] set_paint: buffer too large (" + itos(p_w) + "x" + itos(p_h) + ")");
		return;
	}
	last_paint_size_ = Size2i(static_cast<int>(p_w), static_cast<int>(p_h)); // 收敛/显示基线
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
	queue_redraw(); // 纹理更新 → 重绘（_draw 直接绘制，无子控件 set_texture）
}

// GPU OSR（mac）：CEF OnAcceleratedPaint 交付的 IOSurface 句柄 → 回调内同步
// Metal blit 到自有纹理（commit + waitUntilCompleted）。时序契约（CEF
// cef_render_handler.h）：句柄每帧来自缓冲池、仅本回调内有效、不得缓存——CEF
// 在回调返回后即把 IOSurface 归还缓冲池（可能被下一帧复用/覆写），故拷贝必须
// 在本回调内完成：本函数在回调内 blit 并等待 GPU 完成，才允许 CEF 回收。
// 自有目标纹理（gpu_metal_texture，BGRA8/RGBA8 按 CEF info.format 决定）经
// texture_create_from_extension 导入 RD 供 _draw 直接采样，无 CPU 读回、无
// RD 延迟队列依赖。非 mac 平台 / 非 Metal 渲染器：无操作（保持软件路径，见
// webview_core create_browser）。
void WebPanel::set_accelerated_paint(uint64_t p_handle, uint32_t p_w, uint32_t p_h, AcceleratedPaintFormat p_format) {
#if defined(__APPLE__) && defined(RD_ENABLED) && defined(METAL_ENABLED)
	if (p_handle == 0 || p_w == 0 || p_h == 0) {
		return;
	}
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (rd == nullptr) {
		ERR_PRINT_ONCE("[WebView] GPU OSR: RenderingDevice unavailable (set WEBVIEW_OSR_SOFTWARE=1 to force software path).");
		return;
	}
	// mac 上渲染器可为 Metal（默认）或 Vulkan/MoltenVK（--rendering-driver vulkan）：
	// IOSurface 导入只支持 Metal 上下文驱动，其余渲染器忽略 GPU OSR 保持软件路径。
	RenderingContextDriver *ctx = rd->get_context_driver();
	RenderingContextDriverMetal *metal_ctx = dynamic_cast<RenderingContextDriverMetal *>(ctx);
	if (metal_ctx == nullptr || metal_ctx->get_metal_device() == nullptr) {
		return;
	}
	MTL::Device *device = metal_ctx->get_metal_device();

	// 字节序（CEF info.format，不得硬编码）：源 IOSurface 与目标纹理必须同一格式，
	// 否则 Metal blit 按字节拷贝会得到 R/B 互换的帧。
	const bool is_bgra = p_format == AcceleratedPaintFormat::BGRA8;
	const MTL::PixelFormat mtl_format = is_bgra ? MTL::PixelFormatBGRA8Unorm : MTL::PixelFormatRGBA8Unorm;
	const RenderingDevice::DataFormat rd_format = is_bgra ? RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM : RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM;

	// 尺寸或字节序变化：重建自有 Metal 纹理并重新导入 RD。先解包 RS 纹理再 free RD RID。
	const Size2i new_size(static_cast<int>(p_w), static_cast<int>(p_h));
	if (gpu_metal_texture == nullptr || gpu_size != new_size || gpu_format != p_format) {
		_free_gpu_texture();
		if (gpu_metal_queue == nullptr) {
			gpu_metal_queue = device->newCommandQueue();
			if (gpu_metal_queue == nullptr) {
				ERR_PRINT("[WebView] GPU OSR: failed to create Metal command queue");
				return;
			}
		}
		MTL::TextureDescriptor *desc = MTL::TextureDescriptor::alloc()->init();
		desc->setTextureType(MTL::TextureType2D);
		desc->setPixelFormat(mtl_format);
		desc->setWidth(p_w);
		desc->setHeight(p_h);
		desc->setMipmapLevelCount(1);
		desc->setArrayLength(1);
		desc->setStorageMode(MTL::StorageModePrivate); // 仅 GPU 采样，无需 CPU 访问
		desc->setUsage(MTL::TextureUsageShaderRead);
		MTL::Texture *tex = device->newTexture(desc);
		desc->release();
		if (tex == nullptr) {
			ERR_PRINT("[WebView] GPU OSR: failed to create target texture " + itos(p_w) + "x" + itos(p_h));
			return;
		}
		gpu_metal_texture = tex;
		gpu_format = p_format;
		// 导入 RD（格式匹配时不建 view，RD 直接接管引用，free_rid 时 release）。
		gpu_texture_rid = rd->texture_create_from_extension(
				RenderingDevice::TEXTURE_TYPE_2D,
				rd_format,
				RenderingDevice::TEXTURE_SAMPLES_1,
				RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT,
				reinterpret_cast<uint64_t>(tex),
				p_w, p_h, 1, 1, 1);
		if (!gpu_texture_rid.is_valid()) {
			tex->release(); // 导入失败：释放模块侧引用（未移交 RD）
			gpu_metal_texture = nullptr;
			return;
		}
		gpu_texture.instantiate();
		gpu_texture->set_texture_rd_rid(gpu_texture_rid); // 单线程渲染模式即时，多线程走渲染线程
		gpu_size = new_size;
	}
	if (gpu_metal_texture == nullptr || !gpu_texture_rid.is_valid() || gpu_metal_queue == nullptr) {
		return;
	}

	// 回调内：从 IOSurface 打开源纹理（每帧新句柄，不得缓存/不得回调外访问）。
	// 用 alloc/init 而非 texture2DDescriptor 便捷构造（后者返回 autoreleased 对象，
	// release 会过度释放）。
	MTL::TextureDescriptor *desc = MTL::TextureDescriptor::alloc()->init();
	desc->setTextureType(MTL::TextureType2D);
	desc->setPixelFormat(mtl_format);
	desc->setWidth(p_w);
	desc->setHeight(p_h);
	desc->setMipmapLevelCount(1);
	desc->setArrayLength(1);
	desc->setStorageMode(MTL::StorageModeShared);
	desc->setUsage(MTL::TextureUsageShaderRead);
	MTL::Texture *src = device->newTexture(desc, static_cast<IOSurfaceRef>(reinterpret_cast<void *>(p_handle)), 0);
	desc->release();
	if (src == nullptr) {
		ERR_PRINT("[WebView] GPU OSR: newTexture(IOSurface) failed");
		return;
	}

	// 同步拷贝（关键：CEF 回调返回即归还 IOSurface 缓冲池，blit 必须在本回调内完成）。
	// 模块自有命令队列上 commit + waitUntilCompleted——等待 GPU 读完成后再返回，
	// 不依赖 RD 帧末提交（draw_graph 延迟到帧末执行，届时源表面可能已被 CEF 复用）。
	// 跨队列说明：blit 在本队列写入 gpu_metal_texture，而 RD 在其主队列（device_queue，
	// 无公开 API 可取——get_driver_resource(COMMAND_QUEUE) 对 Metal 返回合成 ID 非指针）
	// 于帧末采样同一纹理；两者均为默认 hazard tracking（Tracked，驱动 base_hazard_tracking），
	// Metal 自动串行化跨队列冲突。残余窗口仅当 GPU 落后一帧以上（上一帧采样仍在执行）时
	// 可能短暂读到半新帧——自愈、单帧、无持久影响；若实机观察到撕裂再引入 ping-pong。
	MTL::CommandBuffer *cb = gpu_metal_queue->commandBuffer();
	cb->retain(); // commandBuffer() 返回 autoreleased 对象，显式持有防依赖回调所在 pool
	MTL::BlitCommandEncoder *enc = cb->blitCommandEncoder();
	enc->copyFromTexture(src, 0, 0, MTL::Origin::Make(0, 0, 0), MTL::Size::Make(p_w, p_h, 1), gpu_metal_texture, 0, 0, MTL::Origin::Make(0, 0, 0));
	enc->endEncoding();
	cb->commit();
	cb->waitUntilCompleted();
	cb->release();
	src->release(); // 源句柄仅回调内有效，GPU 读完成后即释放

	last_paint_size_ = new_size; // 收敛/显示基线（与软件路径共用）
	gpu_path_active = true;
	if (!gpu_osr_logged_ || last_paint_size_ != last_paint_log_size_) {
		// 首帧/尺寸变化日志：resize 收敛证据（面板目标尺寸与纹理尺寸对齐）。
		print_line("[WebView] GPU OSR frame: " + itos(p_w) + "x" + itos(p_h) + (is_bgra ? " BGRA" : " RGBA"));
		gpu_osr_logged_ = true;
		last_paint_log_size_ = last_paint_size_;
	}
	queue_redraw();
#endif
}

void WebPanel::_free_gpu_texture() {
	if (gpu_texture.is_valid()) {
		gpu_texture.unref(); // 先解包：Texture2DRD 析构 free 其 RS 纹理（引用了 RD RID），顺序不能反
	}
	if (gpu_texture_rid.is_valid()) {
		RenderingDevice *rd = RenderingDevice::get_singleton();
		if (rd != nullptr) {
			rd->free_rid(gpu_texture_rid); // 延迟释放：RD 经 frames 队列 dispose 时 release 底层 MTL 纹理
		}
		gpu_texture_rid = RID();
	}
#if defined(__APPLE__) && defined(RD_ENABLED) && defined(METAL_ENABLED)
	gpu_metal_texture = nullptr; // 所有权归 RD RID（free_rid 释放），此处仅清借指针
	if (gpu_metal_queue != nullptr) {
		gpu_metal_queue->release(); // 模块自有对象，必须显式释放
		gpu_metal_queue = nullptr;
	}
#endif
	gpu_size = Size2i(-1, -1);
	gpu_path_active = false;
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
	pending_size_ = size; // 总是记录最新目标（节流窗口内被丢弃时由 process 补发）
	if (!browser_created) {
		// 首次创建浏览器：立即创建（初始尺寸 = 当前布局）。
		const int ret = WebViewManager::get_singleton()->create_browser(browser_id, url, size.x, size.y);
		if (ret == 0) {
			browser_created = true;
			applied_size_ = size;
			last_resize_ms_ = OS::get_singleton()->get_ticks_msec();
			print_line("[WebView] WebPanel browser created: id=" + itos(browser_id) + " url=" + url);
		} else {
			ERR_PRINT("[WebView] WebPanel browser create failed: id=" + itos(browser_id));
		}
		return;
	}
	// 已创建：只记录最新 desired（pending_size_），由 NOTIFICATION_PROCESS 节流下发
	// （见 process 注释：节流合并 + 未收敛尾随重发）。
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
