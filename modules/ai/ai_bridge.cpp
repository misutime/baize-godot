/**************************************************************************/
/*  ai_bridge.cpp                                                         */
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

#include "ai_bridge.h"

#ifdef TOOLS_ENABLED

#include "semantic_ops.h"
#include "semantic_registry.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "scene/main/scene_tree.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

// AI Bridge（AI FIRST P2）：编辑器内 MCP（Model Context Protocol）HTTP server。
//
// 协议：MCP 2026-07-28 无状态规范子集 over HTTP（POST /mcp，JSON-RPC 消息）。
// 能力面（工具/资源）由 SemanticRegistry（共享能力面注册表）提供——WebUI
// （WebBridge）委托迁移列为后续，见方案《02-方案-语义化AI接口层.md》§3.3。
//
// 传输实现用 NetSocket 自管 accept/recv：StreamPeerSocket::poll 的"空缓冲即 FIN"
// 误判不适用持续连接；select 在本环境对 accept 连接数据不可见，直接用非阻塞
// recv 轮询（recv 的 ERR_BUSY 语义准确）。HTTP 帧按原始字节解析（Content-Length
// 大小写不敏感），响应走每连接输出队列跨帧 flush（非阻塞 send 缓冲满不丢弃）。
// 读侧 FIN 与"处理完已收帧/发完响应"解耦：完整请求即使在同帧收到 FIN 也照常应答。
//
// 安全：仅绑 127.0.0.1 + 仅 POST /mcp + application/json + 拒 Origin（阻断浏览器
// CSRF/DNS rebinding）+ 可选 AI_BRIDGE_TOKEN Bearer 鉴权；连接/输入/输出/空闲/
// 每帧工作量均设上限。
//
// 启用：环境变量 AI_BRIDGE_PORT（1-65535；默认 47653；0 关闭）、AI_BRIDGE_TOKEN（可选）。
// 线程：全部主线程（编辑器主循环 poll，与 WebBridge 同线程）。

static constexpr int DEFAULT_PORT = 47653;
static constexpr int RECV_CHUNK = 4096;

static void _append_bytes(Vector<uint8_t> &p_dst, const uint8_t *p_src, int p_len);
static void _consume_bytes(Vector<uint8_t> &p_buf, int p_front_bytes);

AiBridge *AiBridge::singleton = nullptr;

AiBridge *AiBridge::get_singleton() {
	if (!singleton) {
		singleton = memnew(AiBridge);
	}
	return singleton;
}

void AiBridge::free_singleton() {
	if (singleton) {
		singleton->stop();
		memdelete(singleton);
		singleton = nullptr;
	}
}

void AiBridge::start() {
	if (started_) {
		return;
	}
	// 端口：环境变量 AI_BRIDGE_PORT（1-65535）；未设置用默认；0 显式关闭。
	const char *env_port = std::getenv("AI_BRIDGE_PORT");
	int port = DEFAULT_PORT;
	if (env_port) {
		char *end = nullptr;
		const long v = std::strtol(env_port, &end, 10);
		if (end == env_port || *end != '\0' || errno == ERANGE || v < 0 || v > 65535) {
			ERR_PRINT("[AI] AI Bridge 无效 AI_BRIDGE_PORT: '" + String(env_port) + "'（需 0-65535 整数，0 关闭）");
			return;
		}
		port = (int)v;
	}
	if (port <= 0) {
		print_line("[AI] AI Bridge disabled (AI_BRIDGE_PORT=" + (env_port ? String(env_port) : String("unset")) + ")");
		return;
	}
	// 可选鉴权令牌：设置后每个请求必须带 Authorization: Bearer <token>。
	const char *env_token = std::getenv("AI_BRIDGE_TOKEN");
	auth_token_ = env_token ? String(env_token) : String();

	server_ = NetSocket::create();
	if (server_.is_null()) {
		ERR_PRINT("[AI] AI Bridge NetSocket::create failed");
		return;
	}
	// 先 open（create 只建对象）再 bind（与 TCPServer::listen 同序）；
	// open 第三参为 IP::Type&（输出，决定 socket 地址族）。
	IP::Type ip_type = IP::TYPE_ANY;
	Error err = server_->open(NetSocket::Family::INET, NetSocket::TYPE_TCP, ip_type);
	if (err != OK) {
		ERR_PRINT("[AI] AI Bridge socket open failed (error " + itos(err) + ")");
		server_ = Ref<NetSocket>();
		return;
	}
	// 不用 set_reuse_address_enabled：Windows 的 SO_REUSEADDR 允许第二个 socket 绑定
	// 同一端口（端口劫持），多开编辑器时会把 MCP 连接发到错误的实例——宁可干净地
	// bind 失败，由下面的清晰报错引导换端口（显式端口配置，不自动回退）。
	server_->set_blocking_enabled(false);
	// 仅回环：编辑器控制面不允许外部网络访问（无鉴权时任何可达对端即可操作编辑器）。
	err = server_->bind(NetSocket::Address(IPAddress("127.0.0.1"), port));
	if (err != OK) {
		// 端口冲突/不可用：清晰报错（显式配置策略——不静默禁用、不自动换端口）。
		ERR_PRINT("[AI] AI Bridge 启动失败：无法绑定 127.0.0.1:" + itos(port) + "（错误 " + itos(err) + "）。");
		ERR_PRINT("[AI] 端口来源：" + (env_port ? String("环境变量 AI_BRIDGE_PORT=") + String(env_port) : String("默认端口 47653")) + "。");
		ERR_PRINT("[AI] 请用 AI_BRIDGE_PORT=<其他端口> 指定可用端口（0 关闭），或先结束占用该端口的进程（netstat -ano | findstr :" + itos(port) + "）。");
		server_ = Ref<NetSocket>();
		return;
	}
	err = server_->listen(8);
	if (err != OK) {
		ERR_PRINT("[AI] AI Bridge listen failed on 127.0.0.1:" + itos(port) + " (error " + itos(err) + ")");
		server_ = Ref<NetSocket>();
		return;
	}
	if (!start_frame_pump()) {
		// SceneTree 未就绪（如启动失败后的清理路径）：放弃启动，不留半开状态。
		ERR_PRINT("[AI] AI Bridge SceneTree 未就绪，放弃启动");
		server_->close();
		server_ = Ref<NetSocket>();
		return;
	}
	started_ = true;
	print_line("[AI] AI Bridge (MCP HTTP) listening on 127.0.0.1:" + itos(port) + " (POST /mcp, JSON-RPC" + (auth_token_.is_empty() ? String() : String(", Bearer auth")) + ")");
}

void AiBridge::stop() {
	if (!started_) {
		return;
	}
	stop_frame_pump();
	if (server_.is_valid()) {
		server_->close();
		server_ = Ref<NetSocket>();
	}
	clients_.clear();
	started_ = false;
}

// 帧泵：SceneTree::process_frame 驱动（编辑器主循环，主线程）。
bool AiBridge::start_frame_pump() {
	if (pump_driver_) {
		return true;
	}
	SceneTree *st = SceneTree::get_singleton();
	if (!st) {
		return false; // 启动失败/清理路径：SceneTree 尚未创建
	}
	pump_driver_ = st;
	// SceneStringNames 无 process_frame 成员，用字符串连接（与 WebViewManager 一致）。
	st->connect("process_frame", callable_mp(this, &AiBridge::poll));
	return true;
}

void AiBridge::stop_frame_pump() {
	if (!pump_driver_) {
		return;
	}
	// 引擎清理顺序：Main::cleanup 先 OS::delete_main_loop()（SceneTree 析构把
	// singleton 置空），后才执行编辑器级模块反初始化——此处必须防空/防重复断开，
	// 否则桥默认启动时编辑器退出必现空指针崩溃（main/main.cpp:5155/5184）。
	SceneTree *st = SceneTree::get_singleton();
	if (st && st->is_connected("process_frame", callable_mp(this, &AiBridge::poll))) {
		st->disconnect("process_frame", callable_mp(this, &AiBridge::poll));
	}
	pump_driver_ = nullptr;
}

void AiBridge::poll() {
	if (server_.is_null()) {
		return;
	}
	const uint64_t now = Time::get_singleton()->get_ticks_msec();

	// accept 新连接（非阻塞），限量防描述符耗尽。
	while (clients_.size() < MAX_CLIENTS) {
		NetSocket::Address addr;
		Ref<NetSocket> conn = server_->accept(addr);
		if (conn.is_null()) {
			break; // 无 pending 连接
		}
		conn->set_blocking_enabled(false);
		AiClient c;
		c.sock = conn;
		c.last_io_ms = now;
		clients_.append(c);
	}

	// 逐连接：读请求（非阻塞 recv 轮询）→ HTTP 帧解析 → flush 输出 → 清理。
	for (int i = clients_.size() - 1; i >= 0; i--) {
		AiClient &client = clients_.ptrw()[i];
		bool eof = false;
		if (!client.dead) {
			// 读：每帧每连接限量，防对端拖住主线程。
			int frame_read = 0;
			while (frame_read < MAX_READ_PER_FRAME) {
				uint8_t buf[RECV_CHUNK];
				int read = 0;
				Error rerr = client.sock->recv(buf, RECV_CHUNK, read);
				if (rerr == ERR_BUSY) {
					break; // 读空
				}
				if (rerr != OK) {
					client.dead = true; // 非瞬时错误（对端 reset 等）：终态
					break;
				}
				if (read == 0) {
					eof = true; // 对端正常关闭（读侧 FIN）
					break;
				}
				client.last_io_ms = now;
				frame_read += read;
				_append_bytes(client.buffer, buf, read);
				if (client.buffer.size() > MAX_HEADER_BYTES + MAX_BODY_BYTES) {
					client.dead = true; // 输入超限
					break;
				}
			}

			// HTTP 帧处理：原始字节累积，按 Content-Length 完整后处理（支持管线）。
			// 收到读侧 FIN 后仍处理缓冲中的完整帧（half-close 的合法模式）。
			int processed = 0;
			while (!client.dead && processed < MAX_REQUESTS_PER_FRAME) {
				const int hdr_end = _find_header_end(client.buffer);
				if (hdr_end < 0) {
					if (client.buffer.size() > MAX_HEADER_BYTES) {
						client.dead = true;
					}
					break; // headers 未完整（或已超限）
				}
				if (hdr_end + 4 > MAX_HEADER_BYTES) {
					client.dead = true; // 完整但超大的头部
					break;
				}
				const HttpRequestInfo info = _parse_http_frame(client.buffer, hdr_end, auth_token_);
				if (info.reject_status != 0) {
					// 校验失败：回 4xx 并终止连接。
					_http_reject(client, info.reject_status, info.reject_reason);
					break;
				}
				if (!info.valid) {
					client.dead = true; // 帧结构非法（无法定位 body 边界）
					break;
				}
				const int body_start = hdr_end + 4;
				if (info.content_length > MAX_BODY_BYTES) {
					client.dead = true;
					break;
				}
				if (client.buffer.size() < body_start + (int)info.content_length) {
					break; // body 未完整，等更多数据
				}
				// 完整 body 一次性解码（多字节 UTF-8 跨 recv 块也正确）。
				const String body = String::utf8((const char *)client.buffer.ptr() + body_start, (int)info.content_length);
				_consume_bytes(client.buffer, body_start + (int)info.content_length);
				handle_request(client, body);
				processed++;
			}
		}

		// 尽力发送响应队列（EOF/dead 也发——拒绝响应与最后应答必须送达）。
		if (!_flush_out(client)) {
			client.dead = true;
		}

		// 清理：EOF/致命后待输出发完；空闲超时（有未发完输出时延后，防误杀大响应）。
		if ((eof || client.dead)) {
			if (client.out.is_empty() || (now - client.last_io_ms > IDLE_TIMEOUT_MS)) {
				client.sock->close();
				clients_.remove_at(i);
			}
		} else if (client.out.is_empty() && (now - client.last_io_ms > IDLE_TIMEOUT_MS)) {
			client.sock->close();
			clients_.remove_at(i);
		}
	}
}

void AiBridge::handle_request(AiClient &p_client, const String &p_body) {
	const Variant parsed = JSON::parse_string(p_body);
	Dictionary req;
	bool has_id = false;
	Variant id;
	if (parsed.get_type() == Variant::DICTIONARY) {
		req = parsed.operator Dictionary();
		has_id = req.has("id");
		if (has_id) {
			id = req["id"];
		}
	}
	if (parsed.get_type() != Variant::DICTIONARY) {
		// 解析错误：规范要求 id:null 的错误响应。
		Dictionary error;
		error["code"] = -32700;
		error["message"] = "Parse error";
		Dictionary resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = Variant();
		resp["error"] = error;
		_send_http(p_client, resp);
		return;
	}
	// JSON-RPC 信封校验（严格子集）。
	if (req.has("jsonrpc") && req["jsonrpc"] != String("2.0")) {
		_send_jsonrpc_error(p_client, -32600, "Invalid Request: jsonrpc must be \"2.0\"", has_id ? id : Variant(), has_id);
		return;
	}
	if (req.has("method") && req["method"].get_type() != Variant::STRING) {
		_send_jsonrpc_error(p_client, -32600, "Invalid Request: method must be a string", has_id ? id : Variant(), has_id);
		return;
	}
	if (has_id && id.get_type() != Variant::STRING && id.get_type() != Variant::INT && id.get_type() != Variant::FLOAT && id.get_type() != Variant::NIL) {
		_send_jsonrpc_error(p_client, -32600, "Invalid Request: id must be string, number or null", Variant(), false);
		return;
	}
	const String method = req.get("method", "").operator String();
	if (method.is_empty()) {
		// Invalid Request：即使请求无 id 也响应（id:null，JSON-RPC 规范）。
		_send_jsonrpc_error(p_client, -32600, "Invalid Request: missing method", has_id ? id : Variant(), true);
		return;
	}
	Dictionary result = dispatch_mcp(method, req.get("params", Variant()));
	if (!has_id) {
		return; // 通知（无 id）：JSON-RPC 不发响应
	}
	Dictionary resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = id;
	if (result.get("ok", false).operator bool()) {
		resp["result"] = result["result"];
	} else {
		resp["error"] = _jsonrpc_error(result["error"].operator Dictionary());
	}
	_send_http(p_client, resp);
}

void AiBridge::_send_jsonrpc_error(AiClient &p_client, int p_code, const String &p_message, const Variant &p_id, bool p_include_id) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	Dictionary resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = p_include_id ? p_id : Variant();
	resp["error"] = error;
	_send_http(p_client, resp);
}

// ---- MCP 分发（2026-07-28 无状态规范子集）----

Dictionary AiBridge::dispatch_mcp(const String &p_method, const Variant &p_params) {
	Dictionary args;
	if (p_params.get_type() == Variant::DICTIONARY) {
		args = p_params.operator Dictionary();
	}
	// MCP 协议方法。
	if (p_method == "initialize") {
		Dictionary capabilities;
		capabilities["tools"] = Dictionary();
		capabilities["resources"] = Dictionary();
		Dictionary result;
		result["protocolVersion"] = "2026-07-28";
		result["capabilities"] = capabilities;
		Dictionary info;
		info["name"] = "baize-godot-ai";
		info["version"] = "0.1.0";
		result["serverInfo"] = info;
		return _ok(result);
	}
	if (p_method == "ping") {
		return _ok(Dictionary());
	}
	if (p_method == "tools/list") {
		return _ok(_tools_list());
	}
	if (p_method == "tools/call") {
		const String name = args.get("name", "").operator String();
		return _tools_call(name, args.get("arguments", Variant()));
	}
	if (p_method == "resources/list") {
		return _ok(_resources_list());
	}
	if (p_method == "resources/read") {
		return _resources_read(args.get("uri", "").operator String());
	}
	// 直接 JSON-RPC 能力调用（与 P1 TCP 协议兼容）。
	return dispatch_method(p_method, p_params);
}

Dictionary AiBridge::_tools_list() {
	// 工具面 = SemanticRegistry（共享能力面注册表，唯一事实源）。
	Array tools;
	for (const SemanticRegistry::Method &m : SemanticRegistry::methods()) {
		Dictionary t;
		t["name"] = m.name;
		t["description"] = m.description;
		t["inputSchema"] = m.input_schema;
		tools.append(t);
	}
	Dictionary result;
	result["tools"] = tools;
	return result;
}

Dictionary AiBridge::_tools_call(const String &p_name, const Variant &p_arguments) {
	Dictionary result = dispatch_method(p_name, p_arguments);
	// MCP 工具结果包装：{ content: [{type:"text", text: <JSON>}] }。
	Array content;
	Dictionary item;
	item["type"] = "text";
	if (result.get("ok", false).operator bool()) {
		item["text"] = JSON::stringify(result["result"]);
	} else {
		item["text"] = JSON::stringify(result["error"]);
	}
	content.append(item);
	Dictionary out;
	out["content"] = content;
	out["isError"] = !result.get("ok", false).operator bool();
	return _ok(out);
}

Dictionary AiBridge::_resources_list() {
	Array resources;
	auto add_res = [&](const String &p_uri, const String &p_name) {
		Dictionary r;
		r["uri"] = p_uri;
		r["name"] = p_name;
		r["mimeType"] = "application/json";
		resources.append(r);
	};
	add_res("ai://ui/tree", "编辑器 UI 语义树快照");
	add_res("ai://editor/state", "编辑器状态（场景/选中/undo）");
	Dictionary result;
	result["resources"] = resources;
	return result;
}

Dictionary AiBridge::_resources_read(const String &p_uri) {
	Array contents;
	Dictionary item;
	item["uri"] = p_uri;
	item["mimeType"] = "application/json";
	if (p_uri == "ai://ui/tree") {
		item["text"] = JSON::stringify(SemanticOps::get_ui_tree()["result"]);
	} else if (p_uri == "ai://editor/state") {
		item["text"] = JSON::stringify(SemanticOps::get_state()["result"]);
	} else {
		return _err("resource_not_found", "未知资源: " + p_uri);
	}
	contents.append(item);
	Dictionary result;
	result["contents"] = contents;
	return _ok(result);
}

Dictionary AiBridge::_ok(const Variant &p_result) {
	Dictionary d;
	d["ok"] = true;
	d["result"] = p_result;
	return d;
}

Dictionary AiBridge::_err(const String &p_code, const String &p_message) {
	Dictionary d;
	d["ok"] = false;
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	d["error"] = error;
	return d;
}

Dictionary AiBridge::_jsonrpc_error(const Dictionary &p_internal_error) {
	const String code = p_internal_error.get("code", "").operator String();
	const String message = p_internal_error.get("message", "unknown error").operator String();
	Dictionary error;
	if (code == "method_not_found") {
		error["code"] = -32601; // Method not found
	} else if (code == "resource_not_found") {
		error["code"] = -32002; // MCP Resource not found
	} else if (code == "no_scene" || code == "nothing_to_undo" || code == "nothing_to_redo") {
		error["code"] = -32001; // 编辑器状态类错误（非参数问题）
	} else {
		error["code"] = -32602; // Invalid params（control/node/prop 未找到、非法值等）
	}
	error["message"] = message;
	Dictionary data;
	data["code"] = code; // 内部错误码（字符串）置于 data，不污染 JSON-RPC 数值码
	error["data"] = data;
	return error;
}

// ---- 能力面分发（SemanticRegistry 共享；WebBridge 委托迁移列为后续）----

Dictionary AiBridge::dispatch_method(const String &p_method, const Variant &p_params) {
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

void AiBridge::emit_event(const String &p_event_name, const String &p_payload_json) {
	// MCP 事件推送（SSE 事件流）P2 暂缓：工具/资源面已覆盖核心能力。
	// 预留接口：HTTP 连接事件流需 SSE 响应通道（后续实现）。
}

void AiBridge::_send_http(AiClient &p_client, const Dictionary &p_message) {
	_send_http(p_client, JSON::stringify(p_message));
}

void AiBridge::_send_http(AiClient &p_client, const String &p_body) {
	const CharString utf8 = p_body.utf8();
	const String resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + itos(utf8.length()) + "\r\nConnection: keep-alive\r\n\r\n" + p_body;
	const CharString r = resp.utf8();
	if (p_client.out.size() + r.length() > MAX_OUT_BYTES) {
		p_client.dead = true; // 对端消费太慢：队列超限，关连接
		return;
	}
	_append_bytes(p_client.out, (const uint8_t *)r.get_data(), r.length());
}

void AiBridge::_http_reject(AiClient &p_client, int p_status, const String &p_reason) {
	const String resp = "HTTP/1.1 " + itos(p_status) + " " + p_reason + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	const CharString r = resp.utf8();
	_append_bytes(p_client.out, (const uint8_t *)r.get_data(), r.length());
	p_client.dead = true;
}

bool AiBridge::_flush_out(AiClient &p_client) {
	if (p_client.out.is_empty()) {
		return true;
	}
	int offset = 0;
	while (offset < p_client.out.size() && offset < MAX_WRITE_PER_FRAME) {
		int sent = 0;
		Error err = p_client.sock->send(p_client.out.ptr() + offset, p_client.out.size() - offset, sent);
		if (err == ERR_BUSY) {
			break; // 缓冲满：下帧再发
		}
		if (err != OK) {
			return false; // 发送失败：连接不可用
		}
		offset += sent;
	}
	if (offset > 0) {
		_consume_bytes(p_client.out, offset);
		p_client.last_io_ms = Time::get_singleton()->get_ticks_msec();
	}
	return true;
}

int AiBridge::_find_header_end(const Vector<uint8_t> &p_buf) {
	// 查找 \r\n\r\n（4 字节）的字节偏移。
	for (int i = 0; i + 3 < p_buf.size(); i++) {
		if (p_buf[i] == '\r' && p_buf[i + 1] == '\n' && p_buf[i + 2] == '\r' && p_buf[i + 3] == '\n') {
			return i;
		}
	}
	return -1;
}

AiBridge::HttpRequestInfo AiBridge::_parse_http_frame(const Vector<uint8_t> &p_buf, int p_header_end, const String &p_auth_token) {
	HttpRequestInfo info;
	// 头部按 ASCII 解码（请求行 + 字段名均为 ASCII）。
	const String headers = String::utf8((const char *)p_buf.ptr(), p_header_end);
	const Vector<String> lines = headers.split("\r\n");
	if (lines.is_empty()) {
		info.reject_status = 400;
		info.reject_reason = "Bad Request";
		return info;
	}
	// 请求行：POST /mcp HTTP/1.x
	const Vector<String> request_line = lines[0].split(" ");
	if (request_line.size() < 2) {
		info.reject_status = 400;
		info.reject_reason = "Bad Request";
		return info;
	}
	if (request_line[0] != "POST") {
		info.reject_status = 405;
		info.reject_reason = "Method Not Allowed";
		return info;
	}
	if (request_line[1] != "/mcp") {
		info.reject_status = 404;
		info.reject_reason = "Not Found";
		return info;
	}

	bool has_content_type = false;
	bool has_content_length = false;
	bool auth_ok = p_auth_token.is_empty(); // 未启用令牌 = 无需鉴权
	// 逐头校验（字段名大小写不敏感）。
	for (int i = 1; i < lines.size(); i++) {
		const int colon = lines[i].find(":");
		if (colon < 0) {
			continue;
		}
		const String name = lines[i].substr(0, colon).strip_edges().to_lower();
		const String val = lines[i].substr(colon + 1).strip_edges();
		if (name == "content-length") {
			if (has_content_length) {
				info.reject_status = 400; // 重复 Content-Length：请求走私面
				info.reject_reason = "Bad Request: duplicate Content-Length";
				return info;
			}
			if (!val.is_valid_int()) {
				info.reject_status = 400;
				info.reject_reason = "Bad Request: invalid Content-Length";
				return info;
			}
			const int64_t v = val.to_int();
			if (v < 0) {
				info.reject_status = 400;
				info.reject_reason = "Bad Request: invalid Content-Length";
				return info;
			}
			info.content_length = v;
			has_content_length = true;
		} else if (name == "transfer-encoding") {
			info.reject_status = 400; // 不支持 chunked
			info.reject_reason = "Bad Request: Transfer-Encoding unsupported";
			return info;
		} else if (name == "content-type") {
			has_content_type = true;
			if (!val.to_lower().contains("application/json")) {
				info.reject_status = 415;
				info.reject_reason = "Unsupported Media Type";
				return info;
			}
		} else if (name == "origin") {
			// 浏览器跨源请求必带 Origin——本服务只服务非浏览器客户端，一律拒绝，
			// 阻断浏览器 CSRF / DNS rebinding（text/plain 简单请求无预检）。
			info.reject_status = 403;
			info.reject_reason = "Forbidden: browser Origin not allowed";
			return info;
		} else if (name == "authorization") {
			if (p_auth_token.is_empty()) {
				continue; // 未启用令牌：忽略
			}
			if (!val.begins_with("Bearer ") || val.substr(7).strip_edges() != p_auth_token) {
				info.reject_status = 401;
				info.reject_reason = "Unauthorized";
				return info;
			}
			auth_ok = true;
		}
	}
	if (!auth_ok) {
		info.reject_status = 401;
		info.reject_reason = "Unauthorized";
		return info;
	}
	if (!has_content_type) {
		info.reject_status = 415;
		info.reject_reason = "Unsupported Media Type: application/json required";
		return info;
	}
	if (!has_content_length) {
		info.reject_status = 411;
		info.reject_reason = "Length Required";
		return info;
	}
	info.valid = true;
	return info;
}

static void _append_bytes(Vector<uint8_t> &p_dst, const uint8_t *p_src, int p_len) {
	const int old = p_dst.size();
	p_dst.resize(old + p_len);
	memcpy(p_dst.ptrw() + old, p_src, p_len);
}

static void _consume_bytes(Vector<uint8_t> &p_buf, int p_front_bytes) {
	const int remain = p_buf.size() - p_front_bytes;
	if (remain > 0) {
		memmove(p_buf.ptrw(), p_buf.ptr() + p_front_bytes, remain);
	}
	p_buf.resize(remain);
}

#endif // TOOLS_ENABLED
