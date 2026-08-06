// SPDX-License-Identifier: MIT
#pragma once

/**************************************************************************/
/*  provider_server.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "core/io/tcp_server.h"
#include "core/math/vector3.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

#include "modules/websocket/websocket_peer.h"

// Godot Provider 的 Transport 层：WS server + JSON-RPC 分派 + 认证 + 事件下行。
//
// 角色：Godot（编辑器核心）= WS server；Electron 主进程（GodotClient）= client 连回。
//
// 协议（与 @baize/godot-rpc 契约对齐）：
// - 每个 WS text message = 恰好一个 JSON-RPC document；
// - request id 一律 string；batch 显式拒绝（-32600）；server 拒 response 输入；
// - 错误码 -32601/-32602/-32000，内部字符串码入 error.data.code；
// - 能力方法从 Registry 查询分派（find + validate_args + handler）；
// - 事件下行 = notification（selection_changed / node_position_changed）。
//
// 认证：client 首帧 hello（params { token }）校验；token 从 env BAIZE_PROVIDER_TOKEN
// 读（Electron spawn 时下发）；env 缺失时警告并跳过校验（本地 dev 宽松模式）。
//
// 生命周期：EDITOR 级 MessageQueue 第一帧 start()（register_types.cpp）；
// uninitialize 时 stop()。
class ProviderServer : public Object {
	struct Peer {
		Ref<WebSocketPeer> peer;
		bool authenticated = false;
		uint64_t auth_deadline_ms = 0;
		bool dead = false;
		String drop_reason;
		Vector<String> out_queue;
	};

	static constexpr int MAX_CLIENTS = 4;
	static constexpr int MAX_OUT_BYTES = 16 * 1024 * 1024;
	static constexpr int MAX_MSGS_PER_FRAME = 64;
	static constexpr uint64_t AUTH_TIMEOUT_MS = 3000;

	static ProviderServer *singleton;

	Ref<TCPServer> tcp_server_;
	Vector<Peer> peers_;
	String token_; // env BAIZE_PROVIDER_TOKEN（空 = dev 宽松模式）
	int listen_port_ = 0;
	bool started_ = false;
	Object *pump_driver_ = nullptr; // SceneTree::process_frame 连接目标

	bool start_frame_pump();
	void stop_frame_pump();

	// 连接处理。
	void _accept_connections();
	void _poll_peers();
	void _handle_frame(Peer &p_peer, const String &p_text);
	void _send(Peer &p_peer, const Dictionary &p_msg);
	void _flush_out(Peer &p_peer);
	void _drop_peer(int p_index, const String &p_reason);

	// 分派：Registry 透传 + hello 认证。
	Dictionary _dispatch(Peer &p_peer, const String &p_method, const Variant &p_params);
	static Dictionary _jsonrpc_error(const Dictionary &p_internal_error);
	static Dictionary _jsonrpc_error_doc(int p_code, const String &p_message, const Variant &p_data, bool p_include_id, const Variant &p_id = Variant());

	// 事件源（Events 层）：EditorSelection 信号 + 帧轮询 diff。
	void _on_selection_changed();
	void _poll_state_diff();
	Array _tracked_selection_;
	HashMap<ObjectID, Vector3> _tracked_positions_;

public:
	static ProviderServer *get_singleton(); // 惰性创建
	static void free_singleton();

	void start();
	void stop();
	void poll(); // 每帧：accept + poll peers + 分派 + 事件 diff

	int get_listen_port() const { return listen_port_; }
};
