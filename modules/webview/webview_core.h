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
#include <vector>

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
		// id: 浏览器 id;method: JS 侧 CefViewClient.invoke 的方法名(点号命名空间);
		// args: 参数列表(协议约定已字符串化;对象参数由前端 SDK JSON.stringify 后传入)。
		std::function<void(int32_t id, const std::string &method, const std::vector<std::string> &args)> on_invoke_method;
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

	// ---- 输入事件转发(OSR 宿主职责)----
	// CEF OSR 无原生窗口,鼠标/键盘/焦点必须由宿主转发(选型文档 §6.3)。
	// 参数用标量透传(API 面不暴露 CEF 类型):坐标 x/y 为 OSR 视口像素(面板本地坐标),
	// modifiers 为 CEF cef_event_flags_t 位标志(见下方 MOD_* 常量)。

	// 鼠标移动。p_leave=true 表示鼠标离开视口(CEF 需显式 leave 才能收 mouseout)。
	void send_mouse_move(int32_t p_id, int p_x, int p_y, uint32_t p_modifiers, bool p_leave);

	// 鼠标按键。p_button:0=左,1=中,2=右;p_up:true=弹起,false=按下;
	// p_click_count:连续点击次数(CEF 双击/三击语义)。
	void send_mouse_click(int32_t p_id, int p_x, int p_y, uint32_t p_modifiers, int p_button, bool p_up, int p_click_count);

	// 鼠标滚轮。p_delta_x/y:滚动量(正=向上/向左)。
	void send_mouse_wheel(int32_t p_id, int p_x, int p_y, uint32_t p_modifiers, int p_delta_x, int p_delta_y);

	// 键盘事件。p_type:0=RAWKEYDOWN,1=KEYDOWN,2=KEYUP,3=CHAR(与 CEF cef_key_event_type_t
	// 一致);p_windows_key_code:Windows 虚拟键码;p_native_key_code:原生键码(Windows 扫描码,
	// 可传 0);p_character:按键产生的 Unicode 字符(KEYEVENT_CHAR 用,char32_t 标量经 uint32 透传,
	// 补充平面 U+10000+ 由核心层拆 UTF-16 代理对发两次 CHAR);p_unmodified_character:去除同时
	// 按下修饰键(Shift 除外)后的字符(CEF 快捷键判定用,无则传 0);p_focus_on_editable:是否在
	// 页面可编辑元素(CEF 输入法/编辑语义;现由 CEF 回调权威提供,该参数仅作后备)。
	void send_key_event(int32_t p_id, int p_type, uint32_t p_modifiers, int p_windows_key_code, int p_native_key_code, uint32_t p_character, uint32_t p_unmodified_character, bool p_focus_on_editable);

	// 焦点。OSR 视图获得/失去焦点时调用(键盘事件只在有焦点时被 renderer 处理)。
	void set_focus(int32_t p_id, bool p_focus);

	// 事件下行(C++→JS):触发页面 addEventListener 注册的 p_event_name 监听器。
	// p_args 为字符串参数列表(协议约定;事件 payload 由协议层 JSON.stringify 成单字符串)。
	// 返回 true 表示事件已发送到 renderer。
	bool emit_event(int32_t p_id, const std::string &p_event_name, const std::vector<std::string> &p_args);

	// 修饰键位标志(与 CEF cef_event_flags_t 对齐,宿主(Godot 壳层)映射用)。
	static constexpr uint32_t MOD_SHIFT = 2;
	static constexpr uint32_t MOD_CONTROL = 4;
	static constexpr uint32_t MOD_ALT = 8;
	static constexpr uint32_t MOD_LEFT_MOUSE = 16;
	static constexpr uint32_t MOD_MIDDLE_MOUSE = 32;
	static constexpr uint32_t MOD_RIGHT_MOUSE = 64;
	static constexpr uint32_t MOD_COMMAND = 128; // mac Cmd
	static constexpr uint32_t MOD_NUM_LOCK = 256;
	static constexpr uint32_t MOD_IS_KEY_PAD = 512;
	static constexpr uint32_t MOD_IS_REPEAT = 8192;
	static constexpr uint32_t MOD_PRECISION_SCROLLING = 16384;

	// 键盘事件类型(与 CEF cef_key_event_type_t 对齐)。
	static constexpr int KEY_RAWKEYDOWN = 0;
	static constexpr int KEY_KEYDOWN = 1;
	static constexpr int KEY_KEYUP = 2;
	static constexpr int KEY_CHAR = 3;

	// 鼠标按键(与 CEF cef_mouse_button_type_t 对齐)。
	static constexpr int MOUSE_LEFT = 0;
	static constexpr int MOUSE_MIDDLE = 1;
	static constexpr int MOUSE_RIGHT = 2;


	// 注入回调(可在 init 前后任意时刻调用;shutdown 后自动清空)。
	void set_callbacks(const Callbacks &p_callbacks);

	// 查询状态(诊断用):CEF 已初始化且未失败、未关闭。
	bool is_initialized() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

#endif // WEBVIEW_CORE_H
