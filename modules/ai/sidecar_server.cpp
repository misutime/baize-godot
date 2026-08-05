/**************************************************************************/
/*  sidecar_server.cpp                                                    */
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

#include "sidecar_server.h"

#include "semantic_registry.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "scene/main/scene_tree.h"

#ifdef TOOLS_ENABLED

SidecarServer *SidecarServer::singleton = nullptr;

SidecarServer *SidecarServer::get_singleton() {
	if (!singleton) {
		singleton = memnew(SidecarServer);
	}
	return singleton;
}

void SidecarServer::free_singleton() {
	if (singleton) {
		singleton->stop();
		memdelete(singleton);
		singleton = nullptr;
	}
}

// ---- 启动 / 停止 ----

void SidecarServer::start() {
	if (started_) {
		return;
	}
	// BAIZE_SIDECAR：1（默认）= spawn；dev = 外部自管（只 listen，不 spawn）。
	// 决策（2026-08-05）：sidecar 是编辑器地基（Agent/LSP/资产管线宿主），恒启用，不提供关闭路径；
	// 旧值 0 视为弃用（警告后按默认 spawn，避免旧 env 静默异常）。
	const char *env_mode = std::getenv("BAIZE_SIDECAR");
	const String mode = env_mode ? String(env_mode) : String("1");
	if (mode == "0") {
		print_line("[Sidecar] BAIZE_SIDECAR=0 已弃用：sidecar 为编辑器基础组件恒启用（设 BAIZE_SIDECAR=dev 可外部自管）");
	} else if (mode != "dev" && mode != "1") {
		ERR_PRINT("[Sidecar] 无效 BAIZE_SIDECAR: " + mode + "（支持 1/dev，默认 1；按默认 spawn 继续）");
	}

	// listen：仅回环，port 0（OS 分配，spawn 后经 env 下发实际 URL——§4.3 审查修订 P2-1）。
	tcp_server_.instantiate();
	Error err = tcp_server_->listen(0, IPAddress("127.0.0.1"));
	if (err != OK) {
		ERR_PRINT("[Sidecar] 启动失败：无法监听 127.0.0.1（错误 " + itos(err) + "）");
		tcp_server_ = Ref<TCPServer>();
		return;
	}
	listen_port_ = tcp_server_->get_local_port();
	session_start_ms_ = OS::get_singleton()->get_ticks_msec(); // health uptime 基准（dev 模式；spawn 模式由 _spawn_sidecar 覆盖为 spawn 时刻）

	// 令牌：spawn 模式生成（内存，仅 env 下发）；dev 模式要求父环境显式提供（审查修订 P1-5）。
	const char *env_dev_token = std::getenv("BAIZE_SIDECAR_TOKEN");
	if (mode == "dev") {
		if (!env_dev_token || String(env_dev_token).is_empty()) {
			ERR_PRINT("[Sidecar] BAIZE_SIDECAR=dev 需要父环境显式提供 BAIZE_SIDECAR_TOKEN（审查修订 P1-5）");
			ERR_PRINT("[Sidecar] 启动指令示例：BAIZE_SIDECAR=dev BAIZE_SIDECAR_TOKEN=<token> <godot 可执行>");
			stop();
			started_ = true;
			return;
		}
		token_ = String(env_dev_token);
	} else {
		token_ = _generate_token();
		if (token_.is_empty()) {
			// 熵源失败：中止启动（不静默降级）。
			stop();
			started_ = true;
			return;
		}
	}

	if (!start_frame_pump()) {
		ERR_PRINT("[Sidecar] SceneTree 未就绪，放弃启动");
		stop();
		return;
	}
	started_ = true;

	if (mode == "dev") {
		print_line("[Sidecar] dev 模式：监听 ws://127.0.0.1:" + itos(listen_port_) + "，等待外部 sidecar 连接（BAIZE_SIDECAR_TOKEN 已由环境提供）");
	} else {
		_spawn_sidecar();
	}
}

void SidecarServer::stop() {
	stop_frame_pump();
	// 退出编排（§4.4 审查修订 P1-6）：EDITOR 级 uninitialize（SceneTree 已删）时
	// 仍可直发 shutdown + kill 进程树——不依赖模块级顺序。
	if (started_ && !peers_.is_empty()) {
		for (int i = 0; i < peers_.size(); i++) {
			SidecarPeer &peer = peers_.write[i];
			if (peer.authenticated && peer.peer.is_valid()) {
				Dictionary notify;
				notify["jsonrpc"] = "2.0";
				notify["method"] = "sidecar.shutdown";
				// 直接发送（不等帧泵 flush——stop 后不再 poll，审查 P1：shutdown 通知必须实际送达）。
				while (!peer.out_queue.is_empty()) {
					if (peer.peer->send_text(peer.out_queue[0]) != OK) {
						break;
					}
					peer.out_queue.remove_at(0);
				}
				peer.peer->send_text(JSON::stringify(notify));
			}
		}
		// 等 2s 让 sidecar 优雅收尾（发 shutdown 通知 + 等 2s + kill 进程树，§4.4）。
		OS::get_singleton()->delay_usec(2000000);
	}
	if (spawned_) {
		_kill_sidecar();
	}
	if (tcp_server_.is_valid()) {
		tcp_server_->stop();
		tcp_server_ = Ref<TCPServer>();
	}
	for (SidecarPeer &peer : peers_) {
		if (peer.peer.is_valid()) {
			peer.peer->close();
		}
	}
	peers_.clear();
	started_ = false;
	spawned_ = false;
	restart_count_ = 0;
	auto_restart_stopped_ = false;
}

bool SidecarServer::start_frame_pump() {
	if (pump_driver_) {
		return true;
	}
	SceneTree *st = SceneTree::get_singleton();
	if (!st) {
		return false;
	}
	pump_driver_ = st;
	st->connect("process_frame", callable_mp(this, &SidecarServer::poll));
	return true;
}

void SidecarServer::stop_frame_pump() {
	if (!pump_driver_) {
		return;
	}
	// 引擎清理顺序：Main::cleanup 先删 SceneTree 才反初始化 EDITOR 模块——防空/防重复断开。
	SceneTree *st = SceneTree::get_singleton();
	if (st && st->is_connected("process_frame", callable_mp(this, &SidecarServer::poll))) {
		st->disconnect("process_frame", callable_mp(this, &SidecarServer::poll));
	}
	pump_driver_ = nullptr;
}

// ---- spawn 管理（§4.4）----

String SidecarServer::_resolve_node() const {
	const char *env_node = std::getenv("BAIZE_NODE");
	if (env_node && String(env_node).is_empty() == false) {
		return String(env_node);
	}
	return String("node"); // PATH 解析；找不到由 ProcessSupervisor spawn 报错
}

void SidecarServer::_spawn_sidecar() {
	const char *env_entry = std::getenv("BAIZE_SIDECAR_ENTRY");
	if (!env_entry || String(env_entry).is_empty()) {
		ERR_PRINT("[Sidecar] BAIZE_SIDECAR=1 需要 BAIZE_SIDECAR_ENTRY 指向 sidecar 入口脚本/SEA 可执行（开发期如 D:/misutime/104_game/baize-godot/web/runtime/dist/index.js）");
		ERR_PRINT("[Sidecar] 未 spawn；WS 监听保持（ws://127.0.0.1:" + itos(listen_port_) + "），可手动连接");
		return;
	}
	ProcessSupervisor::SpawnOptions opts;
	opts.path = _resolve_node();
	opts.args.push_back(String(env_entry));
	// 项目路径：BAIZE_PROJECT_PATH（spawn 用 env 传递，sidecar 不直接探 FS）。
	const char *env_project = std::getenv("BAIZE_PROJECT_PATH");
	const String project_path = env_project ? String(env_project) : String();
	if (!project_path.is_empty()) {
		opts.cwd = project_path;
	}
	opts.env["BAIZE_GODOT_WS_URL"] = "ws://127.0.0.1:" + itos(listen_port_);
	opts.env["BAIZE_GODOT_TOKEN"] = token_;
	opts.env["BAIZE_SIDECAR"] = "1";
	if (!project_path.is_empty()) {
		opts.env["BAIZE_PROJECT_PATH"] = project_path;
	}
	// 日志：sidecar stdout/stderr → user://logs/sidecar.log（S1 起有界 + token redaction 由 sidecar 侧保证）。
	const String log_dir = OS::get_singleton()->get_user_data_dir().path_join("logs");
	DirAccess::make_dir_recursive_absolute(log_dir);
	if (!DirAccess::dir_exists_absolute(log_dir)) {
		ERR_PRINT("[Sidecar] 日志目录不可用: " + log_dir);
	}
	const String log_file = log_dir.path_join("sidecar.log");
	// 日志有界（审查 P2）：>5MB 轮转为 .1（保留 1 份），避免无界磁盘增长（与注释“有界”一致）。
	if (FileAccess::exists(log_file) && FileAccess::get_size(log_file) > 5 * 1024 * 1024) {
		DirAccess::rename_absolute(log_file, log_file + ".1");
	}
	opts.stdout_file = log_file;
	opts.stderr_file = log_file;

	ProcessSupervisor::ProcessHandle handle;
	Error err = ProcessSupervisor::spawn(opts, handle);
	if (err != OK) {
		// 恒启用决策（2026-08-05）：无 Node 是环境配置错误——明确报错 + 安装指引，不静默、不降级。
		ERR_PRINT("[Sidecar] spawn sidecar 失败：" + opts.path + " " + String(env_entry) + "（错误 " + itos(err) + "）");
		ERR_PRINT("[Sidecar] 请确认：① 已安装 Node.js（https://nodejs.org，SEA 发布前开发期必需）；② BAIZE_SIDECAR_ENTRY 指向 sidecar 入口（如 D:/.../web/runtime/dist/index.js）；③ 或设 BAIZE_NODE 指定 node 可执行文件路径。");
		return;
	}
	sidecar_proc_ = handle;
	session_start_ms_ = handle.spawn_ms; // health uptime 基准 = 子进程启动时刻
	spawned_ = true;
	print_line("[Sidecar] sidecar spawned: " + opts.path + " " + String(env_entry) + " → ws://127.0.0.1:" + itos(listen_port_));
}

void SidecarServer::_kill_sidecar() {
	if (spawned_) {
		ProcessSupervisor::kill_tree(sidecar_proc_);
		ProcessSupervisor::release(sidecar_proc_);
		spawned_ = false;
	}
}

void SidecarServer::_schedule_restart() {
	// 稳定重置（审查 P2）：调度时先按稳定计时清零——hello 时刻的重置检查不足以覆盖
	// “稳定运行 5min 后崩溃”的场景（否则计到第 4 次被误停）。
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (stable_since_ms_ != 0 && now - stable_since_ms_ > STABLE_RESET_MS) {
		restart_count_ = 0;
		stable_since_ms_ = 0;
	}
	// 会话上限：稳定运行 5min 后重置计数；第 4 次崩溃不再自动重启（审查修订 P2-7）。
	if (restart_count_ >= MAX_RESTARTS) {
		if (!auto_restart_stopped_) {
			auto_restart_stopped_ = true;
			ERR_PRINT("[Sidecar] sidecar 连续崩溃 " + itos(MAX_RESTARTS) + " 次，不再自动重启（手动恢复：重启编辑器或设 BAIZE_SIDECAR=dev）");
		}
		return;
	}
	restart_count_++;
	const int backoff_ms = MIN(500 << (restart_count_ - 1), 8000); // 0.5/1/2/4/8s 封顶
	next_spawn_ms_ = now + backoff_ms;
	print_line("[Sidecar] sidecar 掉线，退避重启（第 " + itos(restart_count_) + "/" + itos(MAX_RESTARTS) + " 次，" + itos(backoff_ms) + "ms）");
}

String SidecarServer::_generate_token() {
	// 随机 32B hex（仅存内存，spawn env 下发，不落磁盘/日志——§4.3）。
	Vector<uint8_t> bytes;
	bytes.resize(32);
	const Error err = OS::get_singleton()->get_entropy(bytes.ptrw(), bytes.size());
	if (err != OK) {
		// 熵源不可用：不静默降级（AGENTS.md），由调用方据此中止启动。
		ERR_PRINT("[Sidecar] 随机令牌生成失败（get_entropy 错误 " + itos(err) + "）");
		return String();
	}
	return String::hex_encode_buffer(bytes.ptr(), bytes.size());
}

// ---- 连接处理 ----

void SidecarServer::poll() {
	if (!started_ || tcp_server_.is_null()) {
		return;
	}
	// 独立进程监测（审查 P1）：进程已退出（握手前崩溃/execve 失败/被外部杀）→ 清理 + 退避重启。
	// 与 peer 断开路径幂等：此处 _kill_sidecar 置 spawned_=false，后续 peer close 分支不再重复调度。
	if (spawned_ && !ProcessSupervisor::is_running(sidecar_proc_)) {
		_kill_sidecar();
		_schedule_restart();
	}
	// 崩溃恢复调度：到点重新 spawn（退避后）。
	if (spawned_ == false && restart_count_ > 0 && next_spawn_ms_ != 0) {
		if (OS::get_singleton()->get_ticks_msec() >= next_spawn_ms_) {
			next_spawn_ms_ = 0;
			_spawn_sidecar();
		}
	}
	_accept_connections();
	_poll_peers();
}

void SidecarServer::_accept_connections() {
	while (tcp_server_->is_connection_available()) {
		if (peers_.size() >= MAX_CLIENTS) {
			// 连接数上限：拒绝新连接（§5.3 资源预算）。
			Ref<StreamPeer> sp = tcp_server_->take_connection();
			if (sp.is_valid()) {
				Ref<StreamPeerTCP> tcp = sp;
				if (tcp.is_valid()) {
					tcp->disconnect_from_host();
				}
			}
			continue;
		}
		Ref<WebSocketPeer> peer = Ref<WebSocketPeer>(WebSocketPeer::create());
		if (peer.is_null()) {
			// 审查 P2：必须消费 pending TCP 连接再退出，否则 is_connection_available() 恒真 → 帧泵死循环冻结编辑器。
			Ref<StreamPeer> sp = tcp_server_->take_connection();
			if (sp.is_valid()) {
				Ref<StreamPeerTCP> tcp = sp;
				if (tcp.is_valid()) {
					tcp->disconnect_from_host();
				}
			}
			continue;
		}
		// 资源预算（审查修订 P1-4）：4 MiB 有界 message（语义树快照 ~780KB）；accept_stream 前设置。
		peer->set_inbound_buffer_size(MSG_BUFFER_BYTES);
		peer->set_outbound_buffer_size(MSG_BUFFER_BYTES);
		Error err = peer->accept_stream(tcp_server_->take_connection());
		if (err != OK) {
			continue;
		}
		SidecarPeer sp;
		sp.peer = peer;
		sp.auth_deadline_ms = OS::get_singleton()->get_ticks_msec() + AUTH_TIMEOUT_MS;
		peers_.push_back(sp);
	}
}

void SidecarServer::_poll_peers() {
	for (int i = peers_.size() - 1; i >= 0; i--) {
		SidecarPeer &sp = peers_.write[i]; // 本 fork Vector 无写 operator[]：写入经 write proxy
		if (sp.peer.is_null()) {
			peers_.remove_at(i);
			continue;
		}
		sp.peer->poll();
		const WebSocketPeer::State state = sp.peer->get_ready_state();
		if (state == WebSocketPeer::STATE_CLOSED || state == WebSocketPeer::STATE_CLOSING) {
			// sidecar 掉线 → 崩溃恢复路径（仅对被 spawn 的 sidecar 触发重启）。
			if (sp.authenticated && spawned_) {
				_kill_sidecar();
				_schedule_restart();
			}
			_drop_peer(i, "连接关闭");
			continue;
		}
		// 认证 deadline（3s）：超时未认证 → 断开（防占连接数，§5.3）。
		if (!sp.authenticated && sp.auth_deadline_ms != 0 && OS::get_singleton()->get_ticks_msec() > sp.auth_deadline_ms) {
			ERR_PRINT("[Sidecar] 连接认证超时（3s），断开");
			_drop_peer(i, "认证超时");
			continue;
		}
		// 帧读取：WS text message = 一个 packet（完整 document，§5.1 一帧一文档）。
		int msgs = 0;
		while (!sp.dead && sp.peer.is_valid() && sp.peer->get_available_packet_count() > 0 && msgs < MAX_MSGS_PER_FRAME) {
			const uint8_t *data = nullptr;
			int size = 0;
			const Error perr = sp.peer->get_packet(&data, size);
			if (perr != OK || data == nullptr) {
				sp.dead = true;
				sp.drop_reason = "读帧失败";
				break;
			}
			if (!sp.peer->was_string_packet()) {
				sp.dead = true;
				sp.drop_reason = "非文本帧";
				break;
			}
			// buffer 在下次 get_packet 后失效：立即拷贝为 String。
			const String text = String::utf8((const char *)data, size);
			_handle_frame(sp, text); // 只标记 dead，不在帧处理中改容器（引用安全）
			msgs++;
		}
		if (sp.dead) {
			_drop_peer(i, sp.drop_reason.is_empty() ? "致命状态" : sp.drop_reason);
			continue;
		}
		_flush_out(sp); // 内部可能 drop（指针定位），之后不再用 sp
	}
}

void SidecarServer::_handle_frame(SidecarPeer &p_peer, const String &p_text) {
	// 严格 JSON-RPC 2.0 解析（§5.3 裁决，自做薄层；线级合同 §5.1）。
	JSON json;
	Error err = json.parse(p_text);
	if (err != OK) {
		_send(p_peer, _jsonrpc_error_doc(-32700, "Parse error", Variant(), true));
		return;
	}
	const Variant parsed = json.get_data();
	if (parsed.get_type() == Variant::ARRAY) {
		// batch 显式拒绝（§5.1）。
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: batch 显式拒绝（§5.1 线级合同）", Variant(), true));
		return;
	}
	if (parsed.get_type() != Variant::DICTIONARY) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request", Variant(), true));
		return;
	}
	const Dictionary req = parsed.operator Dictionary();
	if (req.get("jsonrpc", Variant()) != String("2.0")) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: jsonrpc 必须为 \"2.0\"", Variant(), true));
		return;
	}
	if (req.get("method", Variant()).get_type() != Variant::STRING) {
		// 无 method → response 输入：server 拒收（§5.1）。
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: 服务端不接受 response 输入", Variant(), true));
		return;
	}
	const String method = req["method"].operator String();
	const bool has_id = req.has("id");
	if (has_id && req["id"].get_type() != Variant::STRING) {
		_send(p_peer, _jsonrpc_error_doc(-32600, "Invalid Request: request id 必须为 string（§5.1）", Variant(), true));
		return;
	}
	const Variant id = has_id ? req["id"] : Variant();
	const Variant params = req.get("params", Variant());

	// 认证（§4.3）：未认证只接受 sidecar.hello；错误令牌断开 + 告警。
	if (!p_peer.authenticated) {
		if (method != "sidecar.hello") {
			ERR_PRINT("[Sidecar] 未认证连接调用 " + method + "，断开");
			p_peer.dead = true;
			p_peer.drop_reason = "未认证调用: " + method;
			return;
		}
		const Dictionary p = params.get_type() == Variant::DICTIONARY ? params.operator Dictionary() : Dictionary();
		if (p.get("token", String()).operator String() != token_) {
			ERR_PRINT("[Sidecar] sidecar.hello token 校验失败，断开（拒绝非本实例 sidecar）");
			// 审查 P1/P2：先直接发送错误响应（含 data.code，走统一映射）再标记 dead——
			// 只入队会在 _poll_peers 的 dead 检查处被丢弃，Node 端收不到 -32000；
			// 无 id 的 notification 不发送响应（JSON-RPC 通知语义）。
			if (has_id) {
				Dictionary internal_error;
				internal_error["code"] = "unauthorized";
				internal_error["message"] = "token 校验失败";
				Dictionary resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				resp["error"] = _jsonrpc_error(internal_error);
				p_peer.peer->send_text(JSON::stringify(resp));
			}
			p_peer.dead = true;
			p_peer.drop_reason = "token 校验失败";
			return;
		}
		p_peer.authenticated = true;
		p_peer.auth_deadline_ms = 0;
		stable_since_ms_ = OS::get_singleton()->get_ticks_msec();
		if (restart_count_ > 0 && stable_since_ms_ - sidecar_proc_.spawn_ms > STABLE_RESET_MS) {
			restart_count_ = 0; // 稳定 5min：重置崩溃计数（§4.4 审查修订 P2-7）
		}
		print_line("[Sidecar] sidecar 握手成功（token 校验通过）");
	}

	// 分派（通知无响应）。
	const Dictionary result = _dispatch(p_peer, method, params);
	if (!has_id) {
		return;
	}
	if (result.get("ok", false).operator bool()) {
		Dictionary resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["result"] = result.get("result", Variant());
		_send(p_peer, resp);
	} else {
		const Dictionary error = result.get("error", Dictionary()).operator Dictionary();
		const Dictionary json_error = _jsonrpc_error(error);
		Dictionary resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["error"] = json_error;
		_send(p_peer, resp);
	}
}

void SidecarServer::_send(SidecarPeer &p_peer, const Dictionary &p_msg) {
	const String text = JSON::stringify(p_msg);
	// 审查 P2：String::length() 按码点计数，输出预算按 UTF-8 字节计量（中文多字节会超 16 MiB 高水位）。
	const int bytes = text.utf8().length();
	if (p_peer.bytes_out + bytes > MAX_OUT_BYTES) {
		p_peer.dead = true; // 慢客户端：队列超限，响应发完后关（§5.3 high-water）
		return;
	}
	p_peer.out_queue.push_back(text);
	p_peer.bytes_out += bytes;
}

void SidecarServer::_flush_out(SidecarPeer &p_peer) {
	while (!p_peer.out_queue.is_empty()) {
		const String text = p_peer.out_queue[0];
		const Error err = p_peer.peer->send_text(text);
		if (err == OK) {
			p_peer.out_queue.remove_at(0);
			// 复审 P2：扣减必须与 _send 入队同口径（UTF-8 字节），否则中文响应致 bytes_out 虚高误触发 16 MiB 断开。
			p_peer.bytes_out -= text.utf8().length();
		} else {
			p_peer.dead = true; // 发送失败：连接不可用
			break;
		}
	}
	if (p_peer.dead) {
		// 指针比较定位索引（SidecarPeer 无 operator==，Vector::find 不可用）。
		for (int i = 0; i < peers_.size(); i++) {
			if (&peers_[i] == &p_peer) {
				_drop_peer(i, p_peer.drop_reason.is_empty() ? "致命状态（输出队列超限/发送失败）" : p_peer.drop_reason);
				return;
			}
		}
	}
}

void SidecarServer::_drop_peer(int p_index, const String &p_reason) {
	if (p_index < 0 || p_index >= peers_.size()) {
		return;
	}
	SidecarPeer &sp = peers_.write[p_index];
	if (sp.peer.is_valid()) {
		sp.peer->close(1008, p_reason);
	}
	peers_.remove_at(p_index);
}

// ---- 分派（§5.2：sidecar.* 自身方法 + SemanticRegistry 透传）----

Dictionary SidecarServer::_dispatch(SidecarPeer &p_peer, const String &p_method, const Variant &p_params) {
	if (p_method == "sidecar.hello") {
		// hello 幂等语义：每次调用都校验 token（已认证连接上错误 token 同样拒绝，不与握手状态耦合）。
		const Dictionary p = p_params.get_type() == Variant::DICTIONARY ? p_params.operator Dictionary() : Dictionary();
		if (p.get("token", String()).operator String() != token_) {
			return _err("unauthorized", "token 校验失败");
		}
		Dictionary result;
		result["ok"] = true;
		Dictionary payload;
		payload["ok"] = true;
		payload["version"] = "0.1.0";
		result["result"] = payload;
		return result;
	}
	if (p_method == "sidecar.health") {
		Dictionary payload;
		payload["ok"] = true;
		// 审查 P2：dev 模式不经 spawn，sidecar_proc_.spawn_ms 恒 0——用 session_start_ms_（listen/spawn 时刻）。
		payload["uptime_ms"] = OS::get_singleton()->get_ticks_msec() - session_start_ms_;
		Array services;
		payload["services"] = services;
		Dictionary result;
		result["ok"] = true;
		result["result"] = payload;
		return result;
	}
	if (p_method == "sidecar.echo") {
		Dictionary result;
		result["ok"] = true;
		result["result"] = p_params;
		return result;
	}
	if (p_method == "sidecar.subscribe" || p_method == "sidecar.unsubscribe") {
		// 订阅表：S2 事件多目标化后生效（计划 §5.2 表格注释）；S1 仅登记空表回执。
		Dictionary payload;
		payload["ok"] = true;
		Array events;
		payload["events"] = events;
		Dictionary result;
		result["ok"] = true;
		result["result"] = payload;
		return result;
	}
	// SemanticRegistry 透传（能力面唯一事实源，与 AiBridge MCP 工具面同源，§5.2）。
	const SemanticRegistry::Method *m = SemanticRegistry::find(p_method);
	if (!m) {
		return _err("method_not_found", "未注册的方法: " + p_method);
	}
	Dictionary args;
	String verr;
	if (!SemanticRegistry::validate_args(*m, p_params, args, verr)) {
		return _err("invalid_params", verr);
	}
	return m->handler(args);
}

Dictionary SidecarServer::_ok(const Variant &p_result) {
	Dictionary d;
	d["ok"] = true;
	d["result"] = p_result;
	return d;
}

Dictionary SidecarServer::_err(const String &p_code, const String &p_message) {
	Dictionary d;
	d["ok"] = false;
	Dictionary e;
	e["code"] = p_code;
	e["message"] = p_message;
	d["error"] = e;
	return d;
}

Dictionary SidecarServer::_jsonrpc_error(const Dictionary &p_internal_error) {
	const String code = p_internal_error.get("code", "").operator String();
	const String message = p_internal_error.get("message", "unknown error").operator String();
	Dictionary error;
	if (code == "method_not_found") {
		error["code"] = -32601; // Method not found（§5.1）
	} else if (code == "invalid_params") {
		error["code"] = -32602; // Invalid params（§5.1）
	} else {
		error["code"] = -32000; // 业务失败统一码（§5.1）
	}
	error["message"] = message;
	Dictionary data;
	data["code"] = code; // 内部字符串码入 data，不污染数值 error.code（§5.1 审查修订 P1-2）
	error["data"] = data;
	return error;
}

Dictionary SidecarServer::_jsonrpc_error_doc(int p_code, const String &p_message, const Variant &p_data, bool p_include_id, const Variant &p_id) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	if (p_data.get_type() != Variant::NIL) {
		error["data"] = p_data;
	}
	Dictionary resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = p_include_id ? p_id : Variant(); // parse/invalid request：id null（JSON-RPC 规范）
	resp["error"] = error;
	return resp;
}

#endif // TOOLS_ENABLED
