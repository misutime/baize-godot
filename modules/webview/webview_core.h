/**************************************************************************/
/*  webview_core.h                                                        */
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

#ifndef WEBVIEW_CORE_H
#define WEBVIEW_CORE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// C++ 核心层:封装 CefViewCore(CefViewBrowserApp / CefViewBrowserClient)为引擎模块可用的
// 生命周期 / 消息泵 / 浏览器 / OSR / JS 桥 API。这是 C++ 路线的基础切片,WebPanel /
// WebViewManager / SCsub 都依赖本 API 面。
//
// 边界纪律(两层,区别于 4A webview_ffi.h 的 C ABI 时代规则):
// - 公开 API 面(本头文件)保持纯 C++:std::string / std::function 回调,零 Godot 类型
//   (非 GDCLASS、无 Godot 类型)——这是接口契约,WebPanel / WebViewManager 均依赖它。
// - 内部实现(webview_core.cpp)不 include 任何 Godot 头(含 typedefs.h 链):本编译单元
//   必须 include CEF 头,而 CEF 的 net_error 枚举与 Godot 的 enum Error 成员重名
//   (ERR_OUT_OF_MEMORY 等),同一编译单元共存必然 C2365 冲突(与 include 顺序无关),
//   故 Godot 设施不可用于本 .cpp——日志/计时走标准库(stderr / std::chrono),
//   Godot 侧日志由宿主导航层(webview_manager / web_panel)负责。
// 两层共同保持:回调经 std::function 交给宿主,禁止 Godot 对象穿越。
//
// 线程模型:CEF 以 external_message_pump=1 模式集成,全部 API 与回调都在主线程
// (编辑器 UI 线程)发生。pump() 驱动 CefDoMessageLoopWork(),CEF 回调(OnPaint /
// processQueryRequest / loadEnd / OnBeforeClose 等)在 pump 内同步触发,宿主可在
// 回调中安全做纹理操作(4A 已验证)。
//
// 生命周期:WebViewManager 单例持有本对象;init() 在首次 create_browser 前惰性调用;
// shutdown() 在模块卸载时调用。初始化失败为终态,不可重试。
class WebViewCore {
public:
	// 宿主注入的回调集合(仿 4A WvCallbacks,C++ 化)。
	struct Callbacks {
		// id: 浏览器 id;rgba: RGBA8 像素缓冲(w*h*4),仅回调期间有效,宿主必须拷贝;
		// w / h: 像素尺寸(CEF OnPaint 输出 BGRA,本核心层已转 RGBA)。
		std::function<void(int32_t id, const uint8_t *rgba, uint32_t w, uint32_t h)> on_paint;
		// id: 浏览器 id;status: HTTP 状态码(加载错误为 -1);url: 加载的 URL。
		std::function<void(int32_t id, int32_t status, const std::string &url)> on_load_status;
		// id: 浏览器 id;query: JS 侧 window.cefViewQuery 请求体;query_id: 应答句柄
		// (原样传给 respond_query)。
		std::function<void(int32_t id, const std::string &query, int64_t query_id)> on_query;
	};

	WebViewCore();
	~WebViewCore();

	// 不可拷贝。
	WebViewCore(const WebViewCore &) = delete;
	WebViewCore &operator=(const WebViewCore &) = delete;

	// 惰性初始化 CEF,首次 create_browser 前调用;p_exe_dir 必须为绝对路径
	// (CEF 151 硬性要求:browser_subprocess_path / root_cache_path 非空必须是绝对路径,
	// 相对路径会导致初始化失败)。失败为终态:CEF 禁止初始化失败后重试,后续调用全部失效。
	bool init(const std::string &p_exe_dir);

	// 消息泵:主线程每帧调用一次。节流:仅当 CEF 通过 OnScheduleMessagePumpWork 请求
	// 泵送时才实际调用 CefDoMessageLoopWork;初始化后前 60 帧无条件泵送(防消息泵
	// 首帧不通知导致 renderer 不产出)。每帧最多泵送一次。
	void pump();

	// 关闭 CEF:先关闭全部浏览器并有界泵等待异步关闭(OnBeforeClose 递减计数),再
	// CefShutdown。幂等,可重复调用。
	void shutdown();

	// 创建窗口渲染(OSR,软件路径)浏览器。p_id 由调用方分配,必须唯一;w / h 为物理像素,
	// 必须非零。返回 0 成功,-1 失败(未初始化 / id 重复 / 尺寸非法 / CEF 创建失败)。
	int create_browser(int32_t p_id, const std::string &p_url, uint32_t p_w, uint32_t p_h);

	// 调整浏览器尺寸(物理像素)。返回 0 成功,-1 失败(未初始化 / id 不存在 / 尺寸非法
	// ——0 或 >INT_MAX 拒绝,与 create_browser 统一校验)。
	int resize_browser(int32_t p_id, uint32_t p_w, uint32_t p_h);

	// 导航到新 URL。返回 0 成功,-1 失败(未初始化 / id 不存在 / 无主 frame)。
	int navigate_browser(int32_t p_id, const std::string &p_url);

	// 请求关闭浏览器(异步:注册表条目在 OnBeforeClose 到达后移除;幂等)。
	void destroy_browser(int32_t p_id);

	// 应答 JS 查询(on_query 回调给出的 p_query_id)。p_id 为查询所属浏览器 id。
	// 返回 true 表示应答已送达 JS 侧;false 表示 query_id 已失效(已应答 / 已取消 /
	// 浏览器不存在)。
	bool respond_query(int32_t p_id, int64_t p_query_id, bool p_success, const std::string &p_response, int p_error);

	// 注入回调(可在 init 前后任意时刻调用;shutdown 后自动清空)。
	void set_callbacks(const Callbacks &p_callbacks);

	// 查询状态(诊断用):CEF 已初始化且未失败、未关闭。
	bool is_initialized() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

#endif // WEBVIEW_CORE_H
