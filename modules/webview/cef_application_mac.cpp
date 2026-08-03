/**************************************************************************/
/*  cef_application_mac.cpp                                               */
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
/* included in all substantial copies or portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "cef_application_mac.h"

#if defined(__APPLE__)

#include <objc/runtime.h>

// CEF mac 事件集成（include/cef_application_mac.h 的 CefScopedSendingEvent）要求
// NSApplication 实现 CrAppProtocol：isHandlingSendEvent / setHandlingSendEvent
// （cefclient/cefsimple 模板的 CefApplication / SimpleApplication 均提供）。Godot 的
// GodotApplication（platform/macos/godot_application.mm）没有这两个方法——CEF 消息泵
// （CefDoMessageLoopWork）里 CefScopedSendingEvent 构造时发送 unrecognized selector，
// 整个编辑器崩溃（实测）。本文件在模块初始化时用 ObjC runtime 注入这两个方法：
//   - 不改引擎核心平台文件（platform/macos/），CEF 依赖保持在 webview 模块内；
//   - 不引入 CEF 头/SDK include 路径（纯 runtime API，可随本模块 C++17 编译）。
// 进程唯一 NSApplication 实例，静态标志与 CEF 参考实现的实例 ivar 语义等价。
static BOOL g_handling_send_event = NO;

static BOOL is_handling_send_event(id p_self, SEL p_cmd) {
	return g_handling_send_event;
}

static void set_handling_send_event(id p_self, SEL p_cmd, BOOL p_handling) {
	g_handling_send_event = p_handling;
}

void webview_install_cef_application_protocol() {
	Class app_cls = objc_getClass("GodotApplication");
	if (app_cls == nullptr) {
		return; // GodotApplication 未注册（非编辑器/异常路径）：不注入
	}
	class_addMethod(app_cls, sel_registerName("isHandlingSendEvent"), (IMP)is_handling_send_event, "c@:");
	class_addMethod(app_cls, sel_registerName("setHandlingSendEvent:"), (IMP)set_handling_send_event, "v@:c");
}

#endif // __APPLE__
