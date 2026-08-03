/**************************************************************************/
/*  ai_bridge.h                                                           */
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

#include "core/io/net_socket.h"
#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

#include <cstdint>

class Object;

// AI Bridge（AI FIRST P2）：编辑器内 MCP（Model Context Protocol）HTTP server。
//
// 协议：MCP 2026-07-28 无状态规范子集 over HTTP（POST /mcp，JSON-RPC 消息）。
// 工具/资源面由 SemanticRegistry 实现（共享能力面注册表，WebBridge 可后续委托）。
//
// 传输实现用 NetSocket 自管 accept/recv：StreamPeerSocket::poll 把"数据读空后的
// 瞬时空缓冲"误判为对端 FIN 而断开（实测），不适用持续连接场景。
//
// 安全（方案"默认仅本机 + 显式授权"）：仅绑 127.0.0.1；仅接受 POST /mcp 且
// Content-Type: application/json；带 Origin 头的请求一律拒绝（阻断浏览器 CSRF/
// DNS rebinding——跨源 fetch 必带 Origin）；设 AI_BRIDGE_TOKEN 时要求
// Authorization: Bearer <token>。每连接的输入/输出/空闲/每帧工作量均设上限。
//
// 启用：环境变量 AI_BRIDGE_PORT（1-65535；默认 47653；0 关闭）、AI_BRIDGE_TOKEN（可选）。
// 线程：全部主线程（编辑器主循环 poll，与 WebBridge 同线程）。
class AiBridge : public Object {
	struct AiClient {
		Ref<NetSocket> sock;
		Vector<uint8_t> buffer; // HTTP 帧累积缓冲（原始字节，按连接隔离）
		Vector<uint8_t> out; // 待发送响应队列（跨帧 flush，非阻塞 send 缓冲满不丢弃）
		uint64_t last_io_ms = 0; // 最后收/发时间（空闲超时）
		bool dead = false; // 输入/输出超限等致命状态：响应发完后关闭
	};

	// 资源上限（防不可信对端耗尽描述符/内存/主线程时间）。
	static constexpr int MAX_CLIENTS = 16;
	static constexpr int MAX_HEADER_BYTES = 64 * 1024;
	static constexpr int MAX_BODY_BYTES = 4 * 1024 * 1024;
	static constexpr int MAX_OUT_BYTES = 32 * 1024 * 1024;
	static constexpr int MAX_READ_PER_FRAME = 1024 * 1024;
	static constexpr int MAX_WRITE_PER_FRAME = 256 * 1024;
	static constexpr int MAX_REQUESTS_PER_FRAME = 64;
	static constexpr uint64_t IDLE_TIMEOUT_MS = 300000;

	// 单帧 HTTP 请求解析结果（严格子集校验）。
	struct HttpRequestInfo {
		bool valid = false; // 帧结构合法（可继续按 body 处理）
		int reject_status = 0; // 校验失败时的 HTTP 状态码（0 = 通过）
		String reject_reason;
		int64_t content_length = 0;
	};

	static AiBridge *singleton;

	Ref<NetSocket> server_;
	Vector<AiClient> clients_;
	String auth_token_; // AI_BRIDGE_TOKEN（空 = 不要求鉴权）
	bool started_ = false;
	Object *pump_driver_ = nullptr; // SceneTree::process_frame 连接目标（仅作连接状态标志）

	bool start_frame_pump();
	void stop_frame_pump();
	/// 向某客户端输出队列追加 HTTP 响应；队列超限置 dead（连接将被关闭）。
	void _send_http(AiClient &p_client, const Dictionary &p_message);
	void _send_http(AiClient &p_client, const String &p_body);
	/// 发送错误状态响应（4xx）并标记 dead（响应发完后连接关闭）。
	void _http_reject(AiClient &p_client, int p_status, const String &p_reason);
	/// 尽力 flush 输出队列（每帧限量）；返回 false 表示发送失败（连接不可用）。
	bool _flush_out(AiClient &p_client);
	/// 查找 HTTP 头结束位置（\r\n\r\n 的字节偏移），未找到返回 -1。
	static int _find_header_end(const Vector<uint8_t> &p_buf);
	/// 解析请求行 + 头部（方法/路径/Content-Type/Origin/鉴权/Content-Length）。
	static HttpRequestInfo _parse_http_frame(const Vector<uint8_t> &p_buf, int p_header_end, const String &p_auth_token);

public:
	static AiBridge *get_singleton(); // 惰性创建
	static void free_singleton();

	void start(); // 按 AI_BRIDGE_PORT 启动监听（<=0 关闭；SceneTree 未就绪则放弃）
	void stop();
	void poll(); // 每帧：accept + 读请求 + 分发 + flush 输出

	/// 向全部连接广播事件（MCP SSE 事件流，P2 暂缓；接口保留）。
	void emit_event(const String &p_event_name, const String &p_payload_json);

private:
	/// 处理一条完整 HTTP body（JSON-RPC 消息）。
	void handle_request(AiClient &p_client, const String &p_body);
	/// MCP 协议分发（initialize/ping/tools/*/resources/* → 能力面）。
	static Dictionary dispatch_mcp(const String &p_method, const Variant &p_params);
	/// 工具面：MCP 工具定义列表（数据源 = SemanticRegistry）。
	static Dictionary _tools_list();
	/// 工具调用：包装 SemanticRegistry 结果为 MCP content 数组。
	static Dictionary _tools_call(const String &p_name, const Variant &p_arguments);
	/// 资源面定义/读取。
	static Dictionary _resources_list();
	static Dictionary _resources_read(const String &p_uri);
	static Dictionary _ok(const Variant &p_result);
	static Dictionary _err(const String &p_code, const String &p_message);
	/// 内部错误码 → JSON-RPC 数值码（原码放 error.data）。
	static Dictionary _jsonrpc_error(const Dictionary &p_internal_error);
	/// 直接发送 JSON-RPC 错误响应（id:null 或回显请求 id）。
	void _send_jsonrpc_error(AiClient &p_client, int p_code, const String &p_message, const Variant &p_id, bool p_include_id);
	/// 能力面分发（SemanticRegistry 共享后端；WebBridge 委托迁移列为后续）。
	static Dictionary dispatch_method(const String &p_method, const Variant &p_params);
};
