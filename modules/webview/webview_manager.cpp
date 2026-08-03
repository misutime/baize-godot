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

#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "scene/main/scene_tree.h"

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
	cbs.on_paint = &WebViewManager::_on_paint;
	cbs.on_load_status = &WebViewManager::_on_load_status;
	cbs.on_query = &WebViewManager::_on_query;
	cbs.on_invoke_method = &WebViewManager::_on_invoke_method;
	cbs.on_focus_editable_changed = &WebViewManager::_on_focus_editable_changed;
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
}

void WebViewManager::register_panel(WebPanel *p_panel) {
	int32_t id = next_browser_id_++;
	p_panel->set_browser_id(id);
	panels_[id] = p_panel;
}

void WebViewManager::unregister_panel(int32_t p_id) {
	panels_.erase(p_id);
}

int WebViewManager::create_browser(int32_t p_id, const String &p_url, int32_t p_w, int32_t p_h) {
	init_core(); // 惰性：首次 create_browser 时初始化 CEF
	if (!core_.is_initialized()) {
		ERR_PRINT("[WebView] create_browser before core ready.");
		return -1;
	}
	const CharString url = p_url.utf8();
	return core_.create_browser(p_id, url.get_data(), (uint32_t)p_w, (uint32_t)p_h);
}

void WebViewManager::resize_browser(int32_t p_id, int32_t p_w, int32_t p_h) {
	if (core_.is_initialized()) {
		core_.resize_browser(p_id, (uint32_t)p_w, (uint32_t)p_h);
	}
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

// 输入事件转发:面板 GUI 输入 → 核心层 → CEF(纯透传,参数语义见 webview_core.h)。
void WebViewManager::send_mouse_move(int32_t p_id, int32_t p_x, int32_t p_y, uint32_t p_modifiers, bool p_leave) {
	if (!core_.is_initialized()) {
		return;
	}
	core_.send_mouse_move(p_id, p_x, p_y, p_modifiers, p_leave);
}

void WebViewManager::send_mouse_click(int32_t p_id, int32_t p_x, int32_t p_y, uint32_t p_modifiers, int32_t p_button, bool p_up, int32_t p_click_count) {
	if (!core_.is_initialized()) {
		return;
	}
	core_.send_mouse_click(p_id, p_x, p_y, p_modifiers, p_button, p_up, p_click_count);
}

void WebViewManager::send_mouse_wheel(int32_t p_id, int32_t p_x, int32_t p_y, uint32_t p_modifiers, int32_t p_delta_x, int32_t p_delta_y) {
	if (!core_.is_initialized()) {
		return;
	}
	core_.send_mouse_wheel(p_id, p_x, p_y, p_modifiers, p_delta_x, p_delta_y);
}

void WebViewManager::send_key_event(int32_t p_id, int32_t p_type, uint32_t p_modifiers, int32_t p_windows_key_code, int32_t p_native_key_code, uint32_t p_character, uint32_t p_unmodified_character, bool p_focus_on_editable) {
	if (!core_.is_initialized()) {
		return;
	}
	core_.send_key_event(p_id, p_type, p_modifiers, p_windows_key_code, p_native_key_code, p_character, p_unmodified_character, p_focus_on_editable);
}

void WebViewManager::set_focus(int32_t p_id, bool p_focus) {
	if (!core_.is_initialized()) {
		return;
	}
	core_.set_focus(p_id, p_focus);
}

void WebViewManager::emit_event(int32_t p_id, const String &p_event_name, const String &p_payload_json) {
	if (!core_.is_initialized()) {
		return;
	}
	const CharString event_name = p_event_name.utf8();
	const CharString payload = p_payload_json.utf8();
	core_.emit_event(p_id, event_name.get_data(), std::vector<std::string>{ payload.get_data() });
}

// IME 组合文本转发(面板 IME 更新 → CEF)。
void WebViewManager::ime_set_composition(int32_t p_id, const String &p_text, int32_t p_selection_start, int32_t p_selection_end) {
	if (!core_.is_initialized()) {
		return;
	}
	const CharString text = p_text.utf8();
	core_.ime_set_composition(p_id, text.get_data(), static_cast<uint32_t>(p_selection_start), static_cast<uint32_t>(p_selection_end));
}

void WebViewManager::ime_commit_text(int32_t p_id, const String &p_text) {
	if (!core_.is_initialized()) {
		return;
	}
	const CharString text = p_text.utf8();
	core_.ime_commit_text(p_id, text.get_data());
}

void WebViewManager::ime_cancel_composition(int32_t p_id) {
	if (!core_.is_initialized()) {
		return;
	}
	core_.ime_cancel_composition(p_id);
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

// 页面可编辑焦点回调 → 分发到面板(IME 管道激活依据)。
void WebViewManager::_on_focus_editable_changed(int32_t p_id, bool p_focus_on_editable) {
	WebViewManager *mgr = peek_singleton();
	if (!mgr) {
		return;
	}
	WebPanel **slot = mgr->panels_.getptr(p_id);
	if (slot && *slot) {
		(*slot)->set_focus_editable(p_focus_on_editable);
	}
}

void WebViewManager::_on_paint(int32_t p_id, const uint8_t *p_rgba, uint32_t p_w, uint32_t p_h) {
	// 静态回调（C++ 核心层经 std::function 调用）：peek 不创建——单例为 null
	// （teardown 后）时直接返回，防止 get_singleton 复活已释放的单例。
	WebViewManager *mgr = peek_singleton();
	if (!mgr) {
		return;
	}
	WebPanel **slot = mgr->panels_.getptr(p_id);
	if (slot && *slot) {
		(*slot)->set_paint(p_rgba, p_w, p_h); // 回调期间缓冲有效，set_paint 内部拷贝
	}
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
