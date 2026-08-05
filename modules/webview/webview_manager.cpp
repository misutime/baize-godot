/**************************************************************************/
/*  webview_manager.cpp                                                   */
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

#include "webview_manager.h"

#include "web_bridge.h"
#include "web_panel.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

// Windows 平台:焦点双轨修复（SetFocus/命中测试）。Godot 头链已引入 windows.h 相关宏,
// 局部取消 DELETE/PRINT 宏(本 TU 用不到 Key::DELETE/Key::PRINT,防展开破坏枚举引用)。
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DELETE
#undef DELETE
#endif
#ifdef PRINT
#undef PRINT
#endif
#endif

namespace {

// 每帧消息泵驱动器：CEF 初始化成功后挂到 SceneTree::process_frame，使 pump 与面板
// 数量解耦——最后一个面板退出后、异步关闭（OnBeforeClose）送达前仍持续泵送。
// 独立 Object（非 GDCLASS 注册，仅作信号连接目标）自身无状态，转发到单例 pump。
class WebViewPumpDriver : public Object {
public:
	void on_process_frame() {
		WebViewManager *mgr = WebViewManager::peek_singleton();
		if (mgr) {
			mgr->pump();
		}
	}
};

} // namespace

WebViewManager *WebViewManager::singleton = nullptr;

WebViewManager *WebViewManager::get_singleton() {
	if (!singleton) {
		singleton = memnew(WebViewManager);
	}
	return singleton;
}

WebViewManager *WebViewManager::peek_singleton() {
	return singleton; // 可空读取，不创建——静态回调在 teardown 后调用时直接返回，防复活单例
}

void WebViewManager::free_singleton() {
	if (!singleton) {
		return;
	}
	singleton->stop_frame_pump(); // 先摘除每帧泵（SceneTree 可能已销毁，幂等）
	// 先关 CEF（关闭全部浏览器并等待异步关闭），再释放单例。
	singleton->shutdown_core();
	memdelete(singleton);
	singleton = nullptr;
}

void WebViewManager::init_core() {
	if (core_initialized_) {
		return;
	}
	core_initialized_ = true; // CEF 初始化失败为终态，禁止重试
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	WebViewCore::Callbacks cbs;
	cbs.on_load_status = &WebViewManager::_on_load_status;
	cbs.on_query = &WebViewManager::_on_query;
	cbs.on_invoke_method = &WebViewManager::_on_invoke_method;
	core_.set_callbacks(cbs);
	// 核心层日志（stderr）转发到 Godot stdout——GUI 版输出面板只显示 print_line。
	core_.set_log_callback([](const std::string &p_msg) {
		print_line(String::utf8(p_msg.c_str()).strip_edges());
	});
	if (!core_.init(exe_dir.utf8().get_data())) {
		ERR_PRINT("[WebView] CEF core init failed (terminal) — exe_dir=" + exe_dir);
		return;
	}
	// 单例接管每帧泵：挂 SceneTree::process_frame（面板不再各自 pump）。
	start_frame_pump();
	print_line("[WebView] CEF core initialized (C++ route).");
}

void WebViewManager::start_frame_pump() {
	if (pump_driver_) {
		return; // 幂等
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree) {
		// 防御：init_core 由树内面板触发，SceneTree 必然存在；此处仅记录。
		WARN_PRINT("[WebView] SceneTree unavailable, message pump not started.");
		return;
	}
	// 成员是 Object*（头文件不暴露具体类型），此处经局部变量还原完整类型供模板推导。
	auto *driver = memnew(WebViewPumpDriver);
	pump_driver_ = driver;
	tree->connect(SNAME("process_frame"), callable_mp(driver, &WebViewPumpDriver::on_process_frame));
}

void WebViewManager::stop_frame_pump() {
	if (!pump_driver_) {
		return; // 幂等
	}
	auto *driver = static_cast<WebViewPumpDriver *>(pump_driver_);
	SceneTree *tree = SceneTree::get_singleton();
	const Callable cb = callable_mp(driver, &WebViewPumpDriver::on_process_frame);
	if (tree && tree->is_connected(SNAME("process_frame"), cb)) {
		tree->disconnect(SNAME("process_frame"), cb);
	}
	memdelete(driver);
	pump_driver_ = nullptr;
}

void WebViewManager::shutdown_core() {
	core_.shutdown();
}

void WebViewManager::pump() {
	core_.pump();
	poll_focus_return(); // 焦点双轨修复（每帧一次，与面板数无关）
}

void WebViewManager::poll_focus_return() {
	// 窗口模式焦点双轨修复：CEF 子窗口持有 Windows 键盘焦点后，点击 Godot 自绘输入控件
	// （LineEdit 等）不会自动转移 Windows 焦点——前台窗口（Engine）已激活，Windows 不
	// 重新分配焦点，键盘事件继续被 CEF 子窗口吃掉，Godot 输入框收不到输入（实测：点过
	// webui 输入框后，原生 Transform position 输入无反应）。
	// 用 Godot 自身输入状态检测"Godot 侧鼠标按下"：点击 CEF 子窗口（webui）时事件不进
	// Godot，Input 状态不更新；点击 Godot UI 时 Input 记录按下。故 Input 的按下沿 =
	// 点击 Godot UI → SetFocus 把键盘焦点归还主窗口（多面板天然正确：每次按下只判一次，
	// 且无需 Windows 线程输入队列——GetAsyncKeyState 在 Godot 线程恒 0，实测）。
#if defined(_WIN32)
	if (panels_.is_empty()) {
		last_mouse_down_ = false;
		return;
	}
	const bool down = Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT);
	if (!(down && !last_mouse_down_)) {
		last_mouse_down_ = down;
		return;
	}
	// Godot 侧按下沿：归还键盘焦点给主窗口（所有面板同一编辑器主窗口，取第一个面板的）
	for (const KeyValue<int32_t, WebPanel *> &E : panels_) {
		Window *win = E.value->get_window();
		if (win == nullptr) {
			continue;
		}
		const int64_t mh = DisplayServer::get_singleton()->window_get_native_handle(DisplayServerEnums::WINDOW_HANDLE, win->get_window_id());
		if (mh != 0) {
			::SetFocus(reinterpret_cast<HWND>(mh));
			print_line("[WebView] focus-return: Godot 侧鼠标按下 → SetFocus(main) hwnd=" + itos(mh));
		}
		break;
	}
	last_mouse_down_ = down;
#endif
}

void WebViewManager::register_panel(WebPanel *p_panel) {
	int32_t id = next_browser_id_++;
	p_panel->set_browser_id(id);
	panels_[id] = p_panel;
}

void WebViewManager::unregister_panel(int32_t p_id) {
	panels_.erase(p_id);
}

int WebViewManager::create_browser(int32_t p_id, const String &p_url, int32_t p_w, int32_t p_h, void *p_parent_handle) {
	init_core(); // 惰性：首次 create_browser 时初始化 CEF
	if (!core_.is_initialized()) {
		ERR_PRINT("[WebView] create_browser before core ready.");
		return -1;
	}
	const CharString url = p_url.utf8();
	return core_.create_browser(p_id, url.get_data(), (uint32_t)p_w, (uint32_t)p_h, p_parent_handle);
}

void WebViewManager::resize_browser(int32_t p_id, int32_t p_x, int32_t p_y, int32_t p_w, int32_t p_h) {
	if (core_.is_initialized()) {
		core_.resize_browser(p_id, p_x, p_y, (uint32_t)p_w, (uint32_t)p_h);
	}
}

void WebViewManager::set_browser_visible(int32_t p_id, bool p_visible) {
	if (core_.is_initialized()) {
		core_.set_browser_visible(p_id, p_visible);
	}
}

int64_t WebViewManager::get_browser_native_handle(int32_t p_id) {
	if (!core_.is_initialized()) {
		return 0;
	}
	return core_.get_browser_native_handle(p_id);
}

void WebViewManager::destroy_browser(int32_t p_id) {
	if (core_.is_initialized()) {
		core_.destroy_browser(p_id);
	}
}

void WebViewManager::navigate_browser(int32_t p_id, const String &p_url) {
	if (core_.is_initialized()) {
		const CharString url = p_url.utf8();
		core_.navigate_browser(p_id, url.get_data());
	}
}

bool WebViewManager::respond_query(int32_t p_id, int64_t p_query_id, bool p_success, const String &p_response, int p_error) {
	if (!core_.is_initialized()) {
		return false;
	}
	const CharString response = p_response.utf8();
	return core_.respond_query(p_id, p_query_id, p_success, response.get_data(), p_error);
}

// 输入事件转发已删除:窗口模式(非 OSR)下 CEF 原生子窗口直接接收鼠标/键盘/IME,宿主不再转发。

void WebViewManager::emit_event(int32_t p_id, const String &p_event_name, const String &p_payload_json) {
	if (!core_.is_initialized()) {
		return;
	}
	const CharString event_name = p_event_name.utf8();
	const CharString payload = p_payload_json.utf8();
	core_.emit_event(p_id, event_name.get_data(), std::vector<std::string>{ payload.get_data() });
}

// invoke 上行（协议层）：静态回调 → WebBridge 方法注册表分派。
// 注意:String(const char*) 是 Latin-1 解码——CEF 的 std::string 是 UTF-8,必须显式 utf8 构造,
// 否则 JS 传入的中文参数/方法名会乱码(ustring.h:692 append_latin1)。
void WebViewManager::_on_invoke_method(int32_t p_id, const std::string &p_method, const std::vector<std::string> &p_args) {
#ifdef DEV_ENABLED
	// 页面调用日志（诊断级，pro 构建剔除；与 query 同级）：args[0] 为含 req_id 的参数 JSON。
	String args_log = p_args.empty() ? String() : String::utf8(p_args[0].c_str());
	print_line("[WebView] invoke: id=" + itos(p_id) + " method=" + String::utf8(p_method.c_str()) + " args=" + args_log);
#endif
	Vector<String> args;
	args.resize(static_cast<int>(p_args.size()));
	for (int i = 0; i < args.size(); i++) {
		args.write[i] = String::utf8(p_args[i].c_str());
	}
	WebBridge::handle_invoke(p_id, String::utf8(p_method.c_str()), args);
}

void WebViewManager::_on_load_status(int32_t p_id, int32_t p_status, const std::string &p_url) {
	WebViewManager *mgr = peek_singleton(); // 可空读取，不创建（防 teardown 后复活单例）
	if (!mgr) {
		return;
	}
	WebPanel **slot = mgr->panels_.getptr(p_id);
	const String url = String(p_url.c_str());
	if (slot && *slot) {
		// 面板级可观测：保留 load_finished / load_error 日志语义。
		if (p_status >= 0) {
			(*slot)->_on_load_finished(url, p_status);
		} else {
			(*slot)->_on_load_error(url, p_status, "[WebView] load failed");
		}
	} else {
#ifdef DEV_ENABLED
		print_line("[WebView] load status: id=" + itos(p_id) + " status=" + itos(p_status) + " url=" + url);
#endif
	}
}

void WebViewManager::_on_query(int32_t p_id, const std::string &p_query, int64_t p_query_id) {
#ifdef DEV_ENABLED
	print_line("[WebView] query: id=" + itos(p_id) + " query_id=" + itos(p_query_id) + " body=" + String(p_query.c_str()));
#endif
	// 立即经消息路由回传确定性应答（echo 请求体），驱动 JS 侧 onSuccess 回调。
	WebViewManager *mgr = peek_singleton(); // 可空读取，不创建（防 teardown 后复活单例）
	if (!mgr) {
		return;
	}
	mgr->respond_query(p_id, p_query_id, true, "echo: " + String(p_query.c_str()), 0);
}
