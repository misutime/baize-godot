// SPDX-License-Identifier: MIT
#include "register_types.h"

#include "core/object/callable_mp.h"
#include "core/object/message_queue.h"

#include "provider_server.h"

#ifdef TOOLS_ENABLED
static void provider_server_start_deferred() {
	ProviderServer::get_singleton()->start();
}
#endif // TOOLS_ENABLED

// Godot Provider：EDITOR 级第一帧启动（WS server + 能力分派 + 事件源）。
// 启动时序与旧 sidecar_server 一致（MessageQueue 首帧——编辑器核心已就绪）。
void initialize_gd_provider_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		MessageQueue::get_singleton()->push_callable(callable_mp_static(provider_server_start_deferred));
	}
#endif // TOOLS_ENABLED
}

void uninitialize_gd_provider_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		ProviderServer::free_singleton();
	}
#endif // TOOLS_ENABLED
}
