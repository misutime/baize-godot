/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "ai_bridge.h"
#include "sidecar_server.h"

#include "core/object/callable_mp.h"
#include "core/object/message_queue.h"

#ifdef TOOLS_ENABLED
// EditorNode 在 Main::start() 创建（晚于模块初始化），AI Bridge / Sidecar 延迟到第一帧启动。
static void ai_bridge_start_deferred() {
	AiBridge::get_singleton()->start();
}
static void sidecar_server_start_deferred() {
	SidecarServer::get_singleton()->start();
}
#endif // TOOLS_ENABLED

void initialize_ai_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		MessageQueue::get_singleton()->push_callable(callable_mp_static(ai_bridge_start_deferred));
		MessageQueue::get_singleton()->push_callable(callable_mp_static(sidecar_server_start_deferred));
	}
#endif // TOOLS_ENABLED
}

void uninitialize_ai_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		// 退出编排（§4.4 审查修订 P1-6）：sidecar 先停（shutdown 通知 + 等 2s + kill 进程树），
		// 再 AiBridge；CEF 在 SCENE 级随后自然 shutdown（Main::cleanup 顺序：SceneTree→EDITOR→SCENE）。
		SidecarServer::free_singleton();
		AiBridge::free_singleton();
	}
#endif // TOOLS_ENABLED
}
