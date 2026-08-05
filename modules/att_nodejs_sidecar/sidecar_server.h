/**************************************************************************/
/*  sidecar_server.h                                                      */
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

#include "core/io/tcp_server.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

#include "modules/websocket/websocket_peer.h"

#include "process_supervisor.h"

// NodeJS↔Godot WS/JSON-RPC 通道（S1，计划《NodeSidecar落地-方案》§4.3/§4.4/§5）。
//
// 角色：Godot = WS server + spawn/重启/退出管主；sidecar = 服务宿主（client 主动连回）。
//
// 传输：复用引擎 websocket 模块（wslay）：TCPServer::listen(127.0.0.1, port 0) +
// WebSocketPeer::accept_stream + 每帧 poll（参照 EditorDebuggerServerWebSocket）。
//
// 协议：严格 JSON-RPC 2.0（§5.3 裁决：自做解析，不依赖引擎内置 JSONRPC 类——
// 其 handler 返回值无条件进 result、无 error.data、response 只收数值 id）。
// 线级合同（§5.1）：一帧一 document；request id 一律 string；batch 显式拒绝
// （-32600）；server 拒 response 输入；错误码 -32601/-32602/-32000 + data.code。
//
// 认证（§4.3 双令牌修订）：godot_sidecar_token（spawn 模式生成，env 传给 sidecar）
// 或 BAIZE_SIDECAR_TOKEN（dev 模式，父环境显式提供）；hello 首帧校验，认证
// deadline 3s；错误令牌断开 + 告警。
//
// 生命周期（§4.4）：BAIZE_SIDECAR=0|1|dev；断开检测 → 退避重启（0.5/1/2/4/8s，
// 会话上限 3 次，稳定 5min 重置）；退出（EDITOR 级 uninitialize）→ shutdown 通知
// + 等 2s + kill 进程树（ProcessSupervisor）。
class SidecarServer : public Object {
	struct SidecarPeer {
		Ref<WebSocketPeer> peer;
		bool authenticated = false;
		uint64_t auth_deadline_ms = 0; // 认证 deadline（3s），0 = 无
		bool dead = false; // 致命状态：未认证/队列超限等，帧循环末尾统一关闭
		String drop_reason;
		Vector<String> out_queue; // 跨帧 flush（慢客户端不丢帧）
		int64_t bytes_out = 0; // 输出队列字节预算
	};

	// 资源预算（§5.3）：sidecar 1 + 调试客户端少量；认证 3s；buffer 4 MiB（语义树快照 ~780KB）。
	static constexpr int MAX_CLIENTS = 4;
	static constexpr int MAX_OUT_BYTES = 16 * 1024 * 1024;
	static constexpr int MAX_MSGS_PER_FRAME = 64;
	static constexpr uint64_t AUTH_TIMEOUT_MS = 3000;
	static constexpr int MSG_BUFFER_BYTES = 4 * 1024 * 1024;
	// 崩溃重启（§4.4）：退避 500ms×2^n 封顶 8s；会话上限 3；稳定 5min 重置。
	static constexpr int MAX_RESTARTS = 3;
	static constexpr uint64_t STABLE_RESET_MS = 300000;

	static SidecarServer *singleton;

	Ref<TCPServer> tcp_server_;
	Vector<SidecarPeer> peers_;
	String token_; // 本实例令牌（spawn 生成 / dev 从 BAIZE_SIDECAR_TOKEN 读）
	int listen_port_ = 0;

	// spawn 管理（BAIZE_SIDECAR=1）。
	bool spawned_ = false;
	ProcessSupervisor::ProcessHandle sidecar_proc_;
	int restart_count_ = 0;
	uint64_t next_spawn_ms_ = 0;
	uint64_t stable_since_ms_ = 0; // 最近一次成功认证时间（稳定计时）
	uint64_t session_start_ms_ = 0; // health uptime 基准（spawn 时刻；dev = 监听时刻，审查 P2）
	bool auto_restart_stopped_ = false; // 第 4 次崩溃后不再自动重启

	bool started_ = false;
	Object *pump_driver_ = nullptr; // SceneTree::process_frame 连接目标

	bool start_frame_pump();
	void stop_frame_pump();

	// 启动路径。
	String _resolve_node() const; // BAIZE_NODE > PATH node
	bool _spawn_sidecar(); // listen 后：生成 token → ProcessSupervisor::spawn（env 注入）；成功 true，失败 false（调用方退避重试）
	void _schedule_restart(); // 断开 → 退避重启（计数/上限/稳定重置）
	void _kill_sidecar(); // kill_tree（已退出进程跳过）+ release
	String _generate_token(); // 随机 32B hex

	// 连接处理。
	void _accept_connections();
	void _poll_peers();
	void _handle_frame(SidecarPeer &p_peer, const String &p_text);
	void _send(SidecarPeer &p_peer, const Dictionary &p_msg);
	void _flush_out(SidecarPeer &p_peer);
	void _drop_peer(int p_index, const String &p_reason);

	// 分派（§5.2）：sidecar.* 自身方法 + Registry 透传。
	Dictionary _dispatch(SidecarPeer &p_peer, const String &p_method, const Variant &p_params);
	// 内部错误 → JSON-RPC 数值码（§5.1：-32601/-32602/-32000，内部码入 data.code）。
	static Dictionary _jsonrpc_error(const Dictionary &p_internal_error);
	// 构造完整错误响应文档（{ jsonrpc, id, error }）；p_include_id=false 时 id=null（parse/invalid request）。
	static Dictionary _jsonrpc_error_doc(int p_code, const String &p_message, const Variant &p_data, bool p_include_id, const Variant &p_id = Variant());
	static Dictionary _ok(const Variant &p_result);
	static Dictionary _err(const String &p_code, const String &p_message);

public:
	static SidecarServer *get_singleton(); // 惰性创建
	static void free_singleton();

	void start(); // 读 BAIZE_SIDECAR：0 关 / dev 外部自管 / 默认 spawn；listen + spawn
	void stop(); // 退出编排：shutdown 通知 + 等 2s + kill 进程树 + 关 server
	void poll(); // 每帧：accept + poll peers + 帧分派 + 崩溃恢复调度

	int get_listen_port() const { return listen_port_; }
};
