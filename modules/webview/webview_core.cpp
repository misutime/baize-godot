/**************************************************************************/
/*  webview_core.cpp                                                      */
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

#include "webview_core.h"

#if defined(_WIN32)
// windows.h 的 min/max 宏会破坏 CEF 头内的 std::max/std::min(cef_ref_counted.h / cef_types_wrappers.h)。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
// mac 主机进程需显式加载 framework(Windows 由导入库自动加载 DLL);
// 头在 SDK include/wrapper/(CEF_CXX 分支编译时 CPPPATH 已含 sdk_dir/include)。
#include <wrapper/cef_library_loader.h>
#include <crt_externs.h> // _NSGetArgc/_NSGetArgv:CefMainArgs 需要真实 argc/argv
#include <cerrno>       // errno/EEXIST/EWOULDBLOCK: 槽位锁失败分支
#include <cstdlib>      // realpath: 运行时根路径规范化(含 ".." 段)
#include <fcntl.h>      // open(O_CREAT|O_RDWR): 槽位锁文件
#include <sys/file.h>   // flock: 槽位锁(POSIX 单实例标准原语)
#include <sys/param.h>  // PATH_MAX: realpath 缓冲
#include <sys/stat.h>   // mkdir: 槽位 base 目录
#include <unistd.h>     // close: 槽位锁 fd 释放
#else
#error "webview 模块当前仅支持 Windows 与 macOS"
#endif

#include <cef_app.h>
#include <cef_browser.h>
#include <cef_command_line.h>
#include <cef_render_handler.h>

#include <CefViewBrowserApp.h>
#include <CefViewBrowserAppDelegate.h>
#include <CefViewBrowserClient.h>
#include <CefViewBrowserClientDelegate.h>
#include <CefViewCoreProtocol.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// 泵送预热帧数:初始化后前 N 帧无条件泵送,防 CEF 首帧不通知 OnScheduleMessagePumpWork
// 导致 renderer 不产出(4A 实测坑)。

// 浏览器尺寸合法性:0 拒绝(CEF 要求非空视口),>INT_MAX 拒绝(GetViewRect 输出 int
// 坐标,超限强转会变负)。create_browser 与 resize_browser 统一校验。
bool is_valid_browser_size(uint32_t p_w, uint32_t p_h) {
	return p_w > 0 && p_h > 0 && p_w <= static_cast<uint32_t>(INT_MAX) && p_h <= static_cast<uint32_t>(INT_MAX);
}

// 标准库日志出口:本编译单元不得 include 任何 Godot 头(CEF net_error 与 Godot enum Error
// 枚举成员重名,同 TU 共存 C2365),日志直接写 stderr;宿主导航层负责 Godot 侧日志。
// 同时经宿主注入的 log 回调转发(见 WebViewCore::set_log_callback)——GUI 版编辑器输出
// 面板只显示 stdout,不转发则 [webview_core] 日志在 GUI 版不可见。
static WebViewCore::LogCallback s_log_cb;
void log_stderr(const char *p_msg) {
	fputs(p_msg, stderr);
	fflush(stderr);
	if (s_log_cb) {
		s_log_cb(std::string(p_msg));
	}
}

// CEF 虚回调入口的 catch-all 记录:CEF 在禁异常边界外编译,宿主代码(/EHsc)抛出的
// 异常绝不允许穿越回 CEF(未定义行为/崩溃)。所有从 CEF 进入本层的虚回调入口
// 必须包 try/catch(...),捕获后记录回调名与异常信息,再返回默认值。
void log_callback_exception(const char *p_callback, const char *p_what) {
	char buf[512];
	snprintf(buf, sizeof(buf), "[webview_core] CEF callback %s threw: %s\n", p_callback, p_what);
	log_stderr(buf);
}

#if defined(__APPLE__)
// 运行时根目录 = 与编辑器 exe 同级、暂存了 CEF 运行时(framework + helper bundle)的
// 目录。分发契约(stage_webview.py):
//   - 非 bundle 裸可执行文件(bin/godot.macos.editor.dev.arm64):运行时与 exe 同级
//     (bin/,stage_runtime 暂存),运行时根 = exe_dir;
//   - .app bundle 内 exe(bin/godot_macos_editor_dev.app/Contents/MacOS/Godot):运行时
//     打进 bundle 内 Contents/Frameworks(stage_bundles 暂存,CEF mac 标准布局)——
//     seatbelt 沙箱只放行 main bundle 内文件读,运行时在 bundle 外时 helper 子进程
//     dlopen framework 被拒(实测),故必须指向 bundle 内。
// Godot 侧(webview_runtime_path.h)对 UI/字体同规则解析到 Contents/Resources,改契约
// 需两处同步。返回的路径必须规范化(无 ".." 段):CEF 把 framework_dir_path/
// main_bundle_path/browser_subprocess_path 原样传入 helper 的 seatbelt 沙箱 profile
// 路径规则,seatbelt 按规范化路径匹配——带 ".." 的规则匹配不上真实路径(sandbox_mac.mm
// 实测)。realpath 失败时保底返回原路径。
std::string resolve_runtime_root(const std::string &p_exe_dir) {
	const std::string kContentsMacOS = "/Contents/MacOS";
	if (p_exe_dir.size() > kContentsMacOS.size() &&
			p_exe_dir.compare(p_exe_dir.size() - kContentsMacOS.size(), kContentsMacOS.size(), kContentsMacOS) == 0) {
		// bundle 根 = exe_dir 去掉 /Contents/MacOS 后缀(不用 ".." 拼,避免折叠算术出错)
		const std::string bundle_root = p_exe_dir.substr(0, p_exe_dir.size() - kContentsMacOS.size());
		const std::string raw = bundle_root + "/Contents/Frameworks";
		char resolved[PATH_MAX];
		if (realpath(raw.c_str(), resolved) != nullptr) {
			return std::string(resolved);
		}
		return raw;
	}
	return p_exe_dir;
}
#endif

// 单调墙钟毫秒(steady_clock,不随系统时间调整)。
uint64_t now_ms() {
	return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// 用户数据根目录名（per-user 缓存基名）：改名只改此处——
// Windows %LOCALAPPDATA%/<此名>/、mac ~/Library/Caches/<此名>/。
// 注：改名后旧目录残留不影响功能（新实例自动用新基名，槽位锁隔离互不干扰）。
static const char *const kCacheBaseDirName = "baize-godot";

// ---- 实例槽位锁（多实例缓存目录隔离）----
// Chromium 浏览器进程单例以 user data 目录为键,同一目录只能一个浏览器进程
// (CefInitialize 第二实例直接失败)。多实例并行需每个实例独占一个缓存目录——
// 槽位池: base/cef(槽位 0)、base/cef-2、base/cef-3 ... 递增探测第一个空闲槽位。
// 槽位锁用文件锁原语(Chromium ProcessSingleton POSIX 同款):进程退出/崩溃时
// OS 自动释放,槽位自动回收复用,无退出清理代码、无累积。锁文件名与目录名
// 一致(base/cef.lock 对应 base/cef)。句柄为进程级 static:webview_core 析构
// (编辑器退出早期)不释放,防槽位被误判空闲后新实例抢占同一目录。
// 本编译单元不 include Godot 头(见 log_stderr 注释),全部用平台 API。
static int slot_lock_fd = -1; // POSIX
#if defined(_WIN32)
static HANDLE slot_lock_handle = INVALID_HANDLE_VALUE;

// UTF-8 → UTF-16(Windows 路径 API 需要;用户名等路径段可能非 ASCII)。
std::wstring utf8_to_wide(const std::string &p_utf8) {
	if (p_utf8.empty()) {
		return std::wstring();
	}
	const int len = MultiByteToWideChar(CP_UTF8, 0, p_utf8.c_str(), -1, nullptr, 0);
	if (len <= 1) {
		return std::wstring();
	}
	std::wstring out(static_cast<size_t>(len - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, p_utf8.c_str(), -1, out.data(), len);
	return out;
}
#endif

// 尝试独占 p_lock_path。返回 0 = 成功(锁持有到进程退出);
// 1 = 被其他实例占用(试下一槽位); -1 = 创建失败(环境问题,显式报错)。
int try_acquire_slot_lock(const std::string &p_lock_path) {
#if defined(_WIN32)
	// 共享模式 0 = 独占:任何其他进程已打开(含只读)都 ERROR_SHARING_VIOLATION。
	const std::wstring wpath = utf8_to_wide(p_lock_path);
	HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		const DWORD err = GetLastError();
		if (err == ERROR_SHARING_VIOLATION) {
			return 1;
		}
		return -1;
	}
	slot_lock_handle = h;
	return 0;
#else
	const int fd = ::open(p_lock_path.c_str(), O_CREAT | O_RDWR, 0644);
	if (fd < 0) {
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		const int err = errno;
		close(fd);
		// 仅 EWOULDBLOCK 视为竞争(试下一槽位);其他失败(如文件系统不支持锁)
		// 按环境错误显式报错,防 init 把它当"被占"无限探测(审查 P2)。
		return err == EWOULDBLOCK ? 1 : -1;
	}
	slot_lock_fd = fd;
	return 0;
#endif
}

// 确保 base 目录存在(槽位目录与锁文件都放其下);已存在不算错。
bool ensure_dir_exists(const std::string &p_dir) {
#if defined(_WIN32)
	if (!CreateDirectoryW(utf8_to_wide(p_dir).c_str(), nullptr)) {
		return GetLastError() == ERROR_ALREADY_EXISTS;
	}
	return true;
#else
	if (mkdir(p_dir.c_str(), 0755) != 0) {
		return errno == EEXIST;
	}
	return true;
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Impl:全部 CEF 对象与委托实现放在 .cpp(头文件保持纯 C++ API 面)。
//
// 委托实现类(Impl::AppDelegate / Impl::ClientDelegate)是 Impl 的嵌套成员,原因:
// 它们持有 Impl* 并访问 Impl 私有状态;嵌套成员天然拥有对 Impl 的访问权,避免
// 在匿名命名空间类中引用 WebViewCore::Impl(私有嵌套类型)造成访问违例。
//
// 已知坑(4A 实测):不要在 OnBeforeClose 路径加任何锁(曾因锁重入间歇卡死)。
// 本核心层全部 API 与回调同线程(主线程),注册表与计数无锁访问,天然规避。
// ---------------------------------------------------------------------------
struct WebViewCore::Impl {
	// ---- 状态 ----
	Callbacks callbacks;
	bool cef_initialized = false;
	bool cef_failed = false; // 初始化失败为终态,禁止重试
	bool cef_shutdown = false;
#if defined(__APPLE__)
	bool framework_loaded = false; // mac:cef_load_library 成功标志(shutdown 时对应 unload)
#endif

	// ---- 泵节流 ----
	// 原子标志:OnScheduleMessageLoopWork 可能来自任意 CEF 线程(CEF 文档:any thread),
	// delegate 只允许原子置位;主线程 pump() 用 exchange(false) 读取并清除。
	std::atomic<bool> pump_requested{false};

	// ---- CEF 对象 ----
	std::shared_ptr<CefViewBrowserAppDelegateInterface> app_delegate;
	CefRefPtr<CefViewBrowserApp> app;

	// ---- 浏览器注册表 ----
	// 每个浏览器一个 client/delegate(尺寸在创建前已知——4A 验证的时机,GetViewRect
	// 直接读 delegate 自身状态,无需按 browser 反查;也规避了 CreateBrowserSync 期间
	// GetViewRect 早于注册表插入的时序问题)。
	struct BrowserEntry {
		CefRefPtr<CefBrowser> browser;
		CefRefPtr<CefViewBrowserClient> client;
		std::shared_ptr<CefViewBrowserClientDelegateInterface> client_delegate;
		uint32_t width = 0;
		uint32_t height = 0;
		bool closing = false;
		// 页面可编辑元素焦点状态(本浏览器专属;focusedEditableNodeChanged 回调更新,
		// send_key_event 读取——随条目销毁自动清除,不跨浏览器串状态)。
		bool focus_on_editable = false;
	};
	std::unordered_map<int32_t, BrowserEntry> browsers;
	int pending_close = 0; // 待异步关闭的浏览器数(OnBeforeClose 递减)

	// ---- paint 暂存(RGBA;仅回调期间有效) ----
	std::vector<uint8_t> paint_buffer;

	// =======================================================================
	// CefViewBrowserAppDelegateInterface 实现:转发 CefViewBrowserApp 的 3 个钩子。
	// =======================================================================
	class AppDelegate final : public CefViewBrowserAppDelegateInterface {
	public:
		explicit AppDelegate(Impl *p_self)
				: self_(p_self) {}

		void onBeforeCommandLineProcessing(const CefString &p_process_type, CefRefPtr<CefCommandLine> p_command_line) override {
			try {
				// WebDock 页面为本地产物（file:// 加载 React 壳：ESM module script + crossorigin
				// CSS link）。CEF 151 默认 file:// 跨源被 CORS 拦截（module script/CSS 均失败，
				// console 证据：Cross origin requests ... file 不在允许协议列表）——放行同源
				// file:// 资源访问。安全影响：本 CEF 实例内 file:// 页面可读其他本地文件；
				// WebDock 只加载自家产物（index.html + assets，无远程/第三方内容），风险低。
				// （用户裁决 2026-08-03：CEF 开关方案）
				p_command_line->AppendSwitch("allow-file-access-from-files");
#if defined(__APPLE__)
				// NetworkService 在 mac 上启动时会访问钥匙串的 "Chromium Safe Storage"
				// 项(OSCrypt cookie 加密密钥)。ad-hoc 签名每次构建/暂存都变 → CDHash 变 →
				// 钥匙串 ACL 失配 → 每次启动都弹“godot 想访问钥匙串机密”密码框(CEF issue
				// #2692 同款;Brave 开发构建同用此开关)。编辑器 WebDock 不需要持久 cookie,
				// 用 mock keychain 免除弹窗(代价:加密密钥每次启动重生成,旧 cookie 不可解,
				// 对本场景无影响)。
				p_command_line->AppendSwitch("use-mock-keychain");
#endif
			} catch (const std::exception &e) {
				log_callback_exception("AppDelegate::onBeforeCommandLineProcessing", e.what());
			} catch (...) {
				log_callback_exception("AppDelegate::onBeforeCommandLineProcessing", "unknown");
			}
		}

		void onBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> p_command_line) override {
			try {
				// helper(CefViewWing.exe)启动前的命令行调整点;暂无需求。
			} catch (const std::exception &e) {
				log_callback_exception("AppDelegate::onBeforeChildProcessLaunch", e.what());
			} catch (...) {
				log_callback_exception("AppDelegate::onBeforeChildProcessLaunch", "unknown");
			}
		}

		void onScheduleMessageLoopWork(int64_t p_delay_ms) override {
			// CEF 请求一次消息泵工作(节流依据)。可能来自任意 CEF 线程,这里只做
			// 原子置位(relaxed 足够:仅为“有活要泵”的提示,CEF 内部有自己的同步),
			// 不碰任何非原子宿主状态;实际泵送由主循环 pump() 完成。
			try {
				self_->pump_requested.store(true, std::memory_order_relaxed);
			} catch (const std::exception &e) {
				log_callback_exception("AppDelegate::onScheduleMessageLoopWork", e.what());
			} catch (...) {
				log_callback_exception("AppDelegate::onScheduleMessageLoopWork", "unknown");
			}
		}

	private:
		Impl *self_;
	};

	// =======================================================================
	// CefViewBrowserClientDelegateInterface 实现:桥 / load / OSR 回调转发到宿主,
	// 其余不关心的回调给默认(纯虚必须全部实现,语义按 CefViewCore 转发面)。
	// =======================================================================
	class ClientDelegate final : public CefViewBrowserClientDelegateInterface {
	public:
		ClientDelegate(Impl *p_self, int32_t p_id, uint32_t p_w, uint32_t p_h)
				: self_(p_self), id_(p_id), width_(p_w), height_(p_h) {}

		void set_size(uint32_t p_w, uint32_t p_h) {
			width_ = p_w;
			height_ = p_h;
		}

		// ---- 桥(renderer 进程) ----
		void processUrlRequest(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const CefString &p_url) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::processUrlRequest", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::processUrlRequest", "unknown");
			}
		}

		void processQueryRequest(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const CefString &p_query, const int64_t p_query_id) override {
			// JS 侧 window.cefViewQuery(query, callback) → CefViewQueryHandler::OnQuery
			// → 本回调。宿主应答走 WebViewCore::respond_query → client->ResponseQuery。
			try {
				self_->handle_query(id_, p_query.ToString(), p_query_id);
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::processQueryRequest", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::processQueryRequest", "unknown");
			}
		}

		void focusedEditableNodeChanged(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, bool p_focus_on_editable_node) override {
			try {
				// 按浏览器记录可编辑元素焦点状态(键盘事件 focus_on_editable_field 权威来源);
				// 只更新本浏览器条目,不串其他浏览器状态。
				auto entry = self_->browsers.find(id_);
				if (entry != self_->browsers.end()) {
					entry->second.focus_on_editable = p_focus_on_editable_node;
				}
				// 回调宿主(面板据此激活/禁用 IME 管道)。
				if (self_->callbacks.on_focus_editable_changed) {
					self_->callbacks.on_focus_editable_changed(id_, p_focus_on_editable_node);
				}
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::focusedEditableNodeChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::focusedEditableNodeChanged", "unknown");
			}
		}

		void invokeMethodNotify(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const CefString &p_method, const CefRefPtr<CefListValue> &p_arguments) override {
			try {
				// 协议层上行:方法名 + 参数(字符串化)。对象参数由前端 SDK JSON.stringify 传入,
				// 故此处只需处理基础类型 → 字符串;其余类型转 "null" 由协议层 JSON 兜底。
				std::vector<std::string> args;
				if (p_arguments) {
					args.reserve(p_arguments->GetSize());
					for (size_t i = 0; i < p_arguments->GetSize(); i++) {
						const CefValueType t = p_arguments->GetType(i);
						switch (t) {
							case VTYPE_STRING:
								args.push_back(p_arguments->GetString(i).ToString());
								break;
							case VTYPE_INT:
								args.push_back(std::to_string(p_arguments->GetInt(i)));
								break;
							case VTYPE_BOOL:
								args.push_back(p_arguments->GetBool(i) ? "true" : "false");
								break;
							case VTYPE_DOUBLE: {
								// %.17g: double 可往返精度(max_digits10),%g 默认 6 位有效数字会丢精度。
								char buf[64];
								snprintf(buf, sizeof(buf), "%.17g", p_arguments->GetDouble(i));
								args.push_back(buf);
							} break;
							case VTYPE_NULL:
								args.push_back("null");
								break;
							default:
								args.push_back("null"); // 对象/数组等:前端已 JSON.stringify,此处兜底
								break;
						}
					}
				}
				if (self_->callbacks.on_invoke_method) {
					self_->callbacks.on_invoke_method(id_, p_method.ToString(), args);
				}
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::invokeMethodNotify", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::invokeMethodNotify", "unknown");
			}
		}

		void reportJSResult(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const CefString &p_context, const CefRefPtr<CefValue> &p_result) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::reportJSResult", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::reportJSResult", "unknown");
			}
		}

		// ---- 上下文菜单 ----
		void onBeforeContextMenu(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, CefRefPtr<CefContextMenuParams> &p_params, CefRefPtr<CefMenuModel> &p_model) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onBeforeContextMenu", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onBeforeContextMenu", "unknown");
			}
		}

		bool onRunContextMenu(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, CefRefPtr<CefContextMenuParams> &p_params, CefRefPtr<CefMenuModel> &p_model, CefRefPtr<CefRunContextMenuCallback> &p_callback) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onRunContextMenu", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onRunContextMenu", "unknown");
			}
			return false;
		}

		bool onContextMenuCommand(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, CefRefPtr<CefContextMenuParams> &p_params, int p_command_id, CefContextMenuHandler::EventFlags p_event_flags) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onContextMenuCommand", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onContextMenuCommand", "unknown");
			}
			return false;
		}

		void onContextMenuDismissed(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onContextMenuDismissed", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onContextMenuDismissed", "unknown");
			}
		}

		// ---- 显示 ----
		void addressChanged(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const CefString &p_url) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::addressChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::addressChanged", "unknown");
			}
		}

		void titleChanged(CefRefPtr<CefBrowser> &p_browser, const CefString &p_title) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::titleChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::titleChanged", "unknown");
			}
		}

		void faviconURLChanged(CefRefPtr<CefBrowser> &p_browser, const std::vector<CefString> &p_icon_urls) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::faviconURLChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::faviconURLChanged", "unknown");
			}
		}

		bool tooltipMessage(CefRefPtr<CefBrowser> &p_browser, const CefString &p_text) override {
			try {
				return false; // 显示默认 tooltip
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::tooltipMessage", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::tooltipMessage", "unknown");
			}
			return false;
		}

		void fullscreenModeChanged(CefRefPtr<CefBrowser> &p_browser, bool p_fullscreen) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::fullscreenModeChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::fullscreenModeChanged", "unknown");
			}
		}

		void statusMessage(CefRefPtr<CefBrowser> &p_browser, const CefString &p_value) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::statusMessage", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::statusMessage", "unknown");
			}
		}

		void loadingProgressChanged(CefRefPtr<CefBrowser> &p_browser, double p_progress) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::loadingProgressChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::loadingProgressChanged", "unknown");
			}
		}

		void consoleMessage(CefRefPtr<CefBrowser> &p_browser, const CefString &p_message, int p_level) override {
			try {
				// 页面 console → 宿主 stderr（编辑器内嵌场景：页面 JS 错误/注入状态可观测，
				// C0.4 排障实证其诊断价值）。级别：dev 全量转发（诊断可见）；pro 构建只转发
				// error/warning（页面 JS 错误仍可排障，console.log 诊断噪声剔除）。
				// CEF LOGSEVERITY: 0=error 1=warning 2=info/log 3+=verbose。
#ifndef DEV_ENABLED
				if (p_level > 1) {
					return;
				}
#endif
				log_stderr(("[webview_core] console: " + p_message.ToString() + "\n").c_str());
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::consoleMessage", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::consoleMessage", "unknown");
			}
		}

		bool cursorChanged(CefRefPtr<CefBrowser> &p_browser, CefCursorHandle p_cursor, cef_cursor_type_t p_type, const CefCursorInfo &p_custom_cursor_info) override {
			try {
				return false; // 使用默认光标
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::cursorChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::cursorChanged", "unknown");
			}
			return false;
		}

		// ---- 下载(嵌入式浏览器默认取消下载) ----
		void onBeforeDownload(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefDownloadItem> &p_download_item, const CefString &p_suggested_name, CefRefPtr<CefBeforeDownloadCallback> &p_callback) override {
			try {
				if (p_callback) {
					p_callback->Continue("", false);
				}
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onBeforeDownload", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onBeforeDownload", "unknown");
			}
		}

		void onDownloadUpdated(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefDownloadItem> &p_download_item, CefRefPtr<CefDownloadItemCallback> &p_callback) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onDownloadUpdated", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onDownloadUpdated", "unknown");
			}
		}

		// ---- 拖拽 ----
		void draggableRegionChanged(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const std::vector<CefDraggableRegion> &p_regions) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::draggableRegionChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::draggableRegionChanged", "unknown");
			}
		}

		// ---- 焦点 ----
		void takeFocus(CefRefPtr<CefBrowser> &p_browser, bool p_next) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::takeFocus", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::takeFocus", "unknown");
			}
		}

		bool setFocus(CefRefPtr<CefBrowser> &p_browser) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::setFocus", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::setFocus", "unknown");
			}
			return false;
		}

		void gotFocus(CefRefPtr<CefBrowser> &p_browser) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::gotFocus", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::gotFocus", "unknown");
			}
		}

		// ---- JS 对话框(false → CEF 默认原生对话框) ----
		bool onJSDialog(CefRefPtr<CefBrowser> &p_browser, const CefString &p_origin_url, CefJSDialogHandler::JSDialogType p_dialog_type, const CefString &p_message_text, const CefString &p_default_prompt_text, CefRefPtr<CefJSDialogCallback> &p_callback, bool &p_suppress_message) override {
			try {
				p_suppress_message = false;
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onJSDialog", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onJSDialog", "unknown");
			}
			return false;
		}

		bool onBeforeUnloadDialog(CefRefPtr<CefBrowser> &p_browser, const CefString &p_message_text, bool p_is_reload, CefRefPtr<CefJSDialogCallback> &p_callback) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onBeforeUnloadDialog", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onBeforeUnloadDialog", "unknown");
			}
			return false;
		}

		void onResetDialogState(CefRefPtr<CefBrowser> &p_browser) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onResetDialogState", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onResetDialogState", "unknown");
			}
		}

		void onDialogClosed(CefRefPtr<CefBrowser> &p_browser) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onDialogClosed", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onDialogClosed", "unknown");
			}
		}

		// ---- 键盘 ----
		bool onPreKeyEvent(CefRefPtr<CefBrowser> &p_browser, const CefKeyEvent &p_event, CefEventHandle p_os_event, bool *p_is_keyboard_shortcut) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onPreKeyEvent", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onPreKeyEvent", "unknown");
			}
			return false;
		}

		bool onKeyEvent(CefRefPtr<CefBrowser> &p_browser, const CefKeyEvent &p_event, CefEventHandle p_os_event) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onKeyEvent", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onKeyEvent", "unknown");
			}
			return false;
		}

		// ---- 生命周期 ----
		bool onBeforePopup(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, const CefString &p_target_url, const CefString &p_target_frame_name, CefLifeSpanHandler::WindowOpenDisposition p_target_disposition, CefWindowInfo &p_window_info, CefBrowserSettings &p_settings, bool &p_disable_javascript_access) override {
			try {
				// 阻止弹窗:OSR 单视图,编辑器嵌入式浏览器不应开新窗口(window.open 失效)。
				return true;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onBeforePopup", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onBeforePopup", "unknown");
			}
			return true; // 异常时仍阻止弹窗
		}

		void onAfterCreate(CefRefPtr<CefBrowser> &p_browser) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onAfterCreate", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onAfterCreate", "unknown");
			}
		}

		bool doClose(CefRefPtr<CefBrowser> &p_browser) override {
			try {
				return false; // 允许关闭流程继续
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::doClose", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::doClose", "unknown");
			}
			return false;
		}

		bool requestClose(CefRefPtr<CefBrowser> &p_browser) override {
			try {
				return false;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::requestClose", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::requestClose", "unknown");
			}
			return false;
		}

		void onBeforeClose(CefRefPtr<CefBrowser> &p_browser) override {
			// 异步关闭完成:移除注册表条目并递减计数。不加锁(见 Impl 注释)。
			try {
				self_->handle_before_close(id_);
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onBeforeClose", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onBeforeClose", "unknown");
			}
		}

		// ---- 加载 ----
		// loadingStateChanged 载荷(isLoading)不含 HTTP 状态码,与 loadEnd 重复触发;
		// 状态由 loadEnd(HTTP 码)与 loadError(-1)承载(与 4A 一致)。
		void loadingStateChanged(CefRefPtr<CefBrowser> &p_browser, bool p_is_loading, bool p_can_go_back, bool p_can_go_forward) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::loadingStateChanged", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::loadingStateChanged", "unknown");
			}
		}

		void loadStart(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, int p_transition_type) override {
			try {
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::loadStart", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::loadStart", "unknown");
			}
		}

		void loadEnd(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, int p_http_status_code) override {
			try {
				// 仅主 frame 上报整页加载状态;iframe 完成不冒充整页。
				if (p_frame->IsMain()) {
					self_->handle_load_status(id_, p_http_status_code, p_frame->GetURL().ToString());
				}
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::loadEnd", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::loadEnd", "unknown");
			}
		}

		void loadError(CefRefPtr<CefBrowser> &p_browser, CefRefPtr<CefFrame> &p_frame, int p_error_code, const CefString &p_error_msg, const CefString &p_failed_url, bool &p_handled) override {
			try {
				// 仅主 frame 上报;iframe 加载错误不作为整页失败上报。
				if (p_frame->IsMain()) {
					self_->handle_load_status(id_, -1, p_failed_url.ToString());
				}
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::loadError", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::loadError", "unknown");
			}
		}

		// ---- OSR 渲染 ----
		bool getScreenInfo(CefRefPtr<CefBrowser> &p_browser, CefScreenInfo &p_screen_info) override {
			try {
				// M1b:无 DPI 缩放处理(device_scale_factor=1,与 4A 一致;V2 接 DisplayServer 缩放)。
				p_screen_info.device_scale_factor = 1.0f;
				p_screen_info.depth = 24;
				p_screen_info.depth_per_component = 8;
				p_screen_info.is_monochrome = 0;
				return true;
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::getScreenInfo", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::getScreenInfo", "unknown");
			}
			return false;
		}

		void getViewRect(CefRefPtr<CefBrowser> &p_browser, CefRect &p_rect) override {
			try {
				// 尺寸在 create_browser / resize_browser 入口已校验(0 与 >INT_MAX 拒绝),
				// 此处再防御性钳制,确保 CEF 始终拿到非空矩形(不出现 0/负宽高)。
				const uint32_t w = (width_ == 0 || width_ > static_cast<uint32_t>(INT_MAX)) ? 1 : width_;
				const uint32_t h = (height_ == 0 || height_ > static_cast<uint32_t>(INT_MAX)) ? 1 : height_;
				p_rect.x = 0;
				p_rect.y = 0;
				p_rect.width = static_cast<int>(w);
				p_rect.height = static_cast<int>(h);
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::getViewRect", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::getViewRect", "unknown");
			}
		}

		void onPaint(CefRefPtr<CefBrowser> &p_browser, CefRenderHandler::PaintElementType p_type, const CefRenderHandler::RectList &p_dirty_rects, const void *p_buffer, int p_width, int p_height) override {
			try {
				// 只处理主视图(popup 合成后续切片)。注意:cef_paint_element_type_t 是无作用域枚举,
				// 枚举值 PET_VIEW / PET_POPUP 在包含作用域,不能写成 PaintElementType::VIEW。
				if (p_type != PET_VIEW) {
					return;
				}
				if (p_buffer == nullptr || p_width <= 0 || p_height <= 0) {
					return;
				}
				self_->handle_paint(id_, p_buffer, p_width, p_height);
			} catch (const std::exception &e) {
				log_callback_exception("ClientDelegate::onPaint", e.what());
			} catch (...) {
				log_callback_exception("ClientDelegate::onPaint", "unknown");
			}
		}

	private:
		Impl *self_;
		int32_t id_;
		uint32_t width_;
		uint32_t height_;
	};

	// =======================================================================
	// 内部处理(全部在主线程,无锁)。
	// =======================================================================

	// OnPaint → BGRA→RGBA → 宿主回调(4A core.rs bgra_to_rgba 逻辑)。
	void handle_paint(int32_t p_id, const void *p_buffer, int p_width, int p_height) {
		if (!callbacks.on_paint) {
			return;
		}
		const size_t byte_count = static_cast<size_t>(p_width) * static_cast<size_t>(p_height) * 4;
		if (paint_buffer.size() < byte_count) {
			paint_buffer.resize(byte_count);
		}
		// CEF OnPaint 输出 BGRA(上左原点);交换 R/B 得到 RGBA。
		const uint8_t *src = static_cast<const uint8_t *>(p_buffer);
		for (size_t i = 0; i < byte_count; i += 4) {
			paint_buffer[i + 0] = src[i + 2]; // R
			paint_buffer[i + 1] = src[i + 1]; // G
			paint_buffer[i + 2] = src[i + 0]; // B
			paint_buffer[i + 3] = src[i + 3]; // A
		}
		callbacks.on_paint(p_id, paint_buffer.data(), static_cast<uint32_t>(p_width), static_cast<uint32_t>(p_height));
	}

	void handle_query(int32_t p_id, const std::string &p_query, int64_t p_query_id) {
		if (callbacks.on_query) {
			callbacks.on_query(p_id, p_query, p_query_id);
		}
	}

	void handle_load_status(int32_t p_id, int32_t p_status, const std::string &p_url) {
		if (callbacks.on_load_status) {
			callbacks.on_load_status(p_id, p_status, p_url);
		}
	}

	// OnBeforeClose:移除条目;仅当该条目是宿主主动关闭(entry.closing)才递减 pending_close——
	// 外部/JS 关闭的浏览器(entry.closing=false)不得抵掉宿主关闭计数,否则 shutdown 会提前。
	// 条目从 map 移除前先读 closing,再 erase。
	void handle_before_close(int32_t p_id) {
		auto it = browsers.find(p_id);
		if (it == browsers.end()) {
			return;
		}
		const bool was_host_closing = it->second.closing;
		browsers.erase(it);
		if (was_host_closing && pending_close > 0) {
			pending_close--;
		}
	}
};

// ---------------------------------------------------------------------------
// WebViewCore
// ---------------------------------------------------------------------------

WebViewCore::WebViewCore()
		: impl_(new Impl()) {}

WebViewCore::~WebViewCore() {
	shutdown(); // 宿主未显式 shutdown 时兜底(幂等)
}

bool WebViewCore::init(const std::string &p_exe_dir) {
	if (impl_ == nullptr) {
		return false;
	}
	if (impl_->cef_initialized && !impl_->cef_shutdown) {
		return true; // 幂等
	}
	if (impl_->cef_failed || impl_->cef_shutdown) {
		return false; // 终态,不可重试
	}
	if (p_exe_dir.empty()) {
		log_stderr("[webview_core] init: exe_dir required (must be absolute)\n");
		return false;
	}

#if defined(__APPLE__)
	// mac 主机进程必须显式加载 framework 才能调用任何 CEF API(Windows 由导入库在进程
	// 启动时自动加载 DLL)——wrapper 的 C API 经全局函数表分发,未加载即为 NULL 指针,
	// 下方第一个 CEF 调用(CefCommandLine::GetGlobalCommandLine)即崩溃。
	// 路径与 stage 暂存布局一致:<runtime_root>/Chromium Embedded Framework.framework/Chromium Embedded Framework。
	const std::string runtime_root = resolve_runtime_root(p_exe_dir);
	const std::string framework_dir = runtime_root + "/Chromium Embedded Framework.framework";
	const std::string framework_path = framework_dir + "/Chromium Embedded Framework";
	if (cef_load_library(framework_path.c_str()) != 1) {
		log_stderr("[webview_core] init: cef_load_library failed (framework not found or invalid)\n");
		impl_->cef_failed = true; // 终态
		return false;
	}
	impl_->framework_loaded = true;
#endif

	// 防御:若本 exe 以 CEF 子进程方式启动(命令行含 --type),走 CefExecuteProcess 而非
	// 初始化。正常不会发生——browser_subprocess_path 指向 CefViewWing(Windows exe /
	// mac bundle 内可执行文件),子进程由它承担。
#if defined(_WIN32)
	CefMainArgs main_args(static_cast<HINSTANCE>(GetModuleHandleW(nullptr)));
#elif defined(__APPLE__)
	// mac 的 CefMainArgs 需要真实 argc/argv;编辑器是非 bundle 可执行文件,从运行时
	// 全局取(main 的形参不可达,标准做法)。
	CefMainArgs main_args(*_NSGetArgc(), *_NSGetArgv());
#endif
	CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
	if (command_line && command_line->HasSwitch("type")) {
		int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
#ifdef DEV_ENABLED
		log_stderr("[webview_core] init: launched as CEF subprocess, CefExecuteProcess handled it\n");
#endif
#if defined(__APPLE__)
		if (impl_->framework_loaded) {
			cef_unload_library(); // 失败出口:与 load 对称,防 framework 驻留到进程退出
			impl_->framework_loaded = false;
		}
#endif
		impl_->cef_failed = true; // 本进程不是浏览器进程,禁止初始化(终态)
		return false;
	}

	// CEF 151 硬性要求:非空路径必须是绝对路径(相对路径初始化失败)。
#if defined(_WIN32)
	const std::string subprocess_path = p_exe_dir + "/CefViewWing.exe"; // 必须与 helper 同目录,不变
#elif defined(__APPLE__)
	// mac helper 是 bundle:指向 bundle 内可执行文件。CEF 按 CEF_HELPER_APP_SUFFIXES
	// 从该路径推导 " (Alerts)/(GPU)/(Plugin)/(Renderer)" 后缀的其余 helper bundle,
	// 全部随 framework 同级分发(stage 暂存到 runtime_root)。
	const std::string subprocess_path = runtime_root + "/CefViewWing.app/Contents/MacOS/CefViewWing";
#endif

	// 缓存目录 = 实例槽位池(多实例并行):固定 base 下 cef / cef-2 / cef-3 ...,
	// 递增探测第一个空闲槽位(文件锁,进程退出自动释放可复用)。Chromium 浏览器
	// 进程单例以 user data 目录为键,同一目录只能一个浏览器进程——多开编辑器
	// (不同项目)必须各自独占槽位目录。单实例行为与旧固定目录完全一致(槽位 0)。
	// base 名 = kCacheBaseDirName(Windows:%LOCALAPPDATA%/下,GetEnvironmentVariableW
	// 取 UTF-16 再转 UTF-8,CefString 的 std::string 赋值按 UTF-8 处理;
	// mac:~/Library/Caches/ 下)。取不到时回退 exe_dir/webview 并警告。
	std::string cache_base;
#if defined(_WIN32)
	const DWORD env_len = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
	if (env_len > 1) { // >1:值非空且含结尾 '\0'(0 或 1 = 未设置/空值,走回退)
		std::vector<wchar_t> env_buf(env_len);
		if (GetEnvironmentVariableW(L"LOCALAPPDATA", env_buf.data(), env_len) > 0) {
			const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, env_buf.data(), -1, nullptr, 0, nullptr, nullptr);
			if (utf8_len > 1) {
				cache_base.resize(static_cast<size_t>(utf8_len - 1));
				WideCharToMultiByte(CP_UTF8, 0, env_buf.data(), -1, cache_base.data(), utf8_len, nullptr, nullptr);
				cache_base += "/";
				cache_base += kCacheBaseDirName;
			}
		}
	}
#elif defined(__APPLE__)
	const char *home = getenv("HOME");
	if (home != nullptr && *home != '\0') {
		cache_base = std::string(home) + "/Library/Caches/" + kCacheBaseDirName;
	} else {
		log_stderr("[webview_core] init: warning: HOME unavailable, cache base falls back to exe_dir/webview\n");
	}
#endif
	if (cache_base.empty()) {
		cache_base = p_exe_dir + "/webview"; // 回退:与原行为一致
		log_stderr("[webview_core] init: warning: cache path env unavailable, cache base falls back to exe_dir/webview\n");
	}
	if (!ensure_dir_exists(cache_base)) {
		log_stderr(("[webview_core] init: cannot create cache base " + cache_base + "\n").c_str());
#if defined(__APPLE__)
		if (impl_->framework_loaded) { // 与 CefInitialize 失败出口对称:已 load 的 framework 必须显式卸载
			cef_unload_library();
			impl_->framework_loaded = false;
		}
#endif
		return false;
	}
	std::string cache_path;
	for (int slot = 0;; slot++) {
		const std::string dir = slot == 0 ? (cache_base + "/cef") : (cache_base + "/cef-" + std::to_string(slot + 1));
		const std::string lock_path = dir + ".lock"; // cef.lock / cef-2.lock,与目录同名
		const int rc = try_acquire_slot_lock(lock_path);
		if (rc == 0) {
			cache_path = dir;
			break;
		}
		if (rc == -1) {
			log_stderr(("[webview_core] init: cannot create slot lock " + lock_path + "\n").c_str());
#if defined(__APPLE__)
			if (impl_->framework_loaded) { // 同上:失败出口卸载 framework,防泄漏到进程结束
				cef_unload_library();
				impl_->framework_loaded = false;
			}
#endif
			return false;
		}
		// rc == 1:被其他实例占用,试下一槽位。无上限:并发 N 实例占槽位 0..N-1,
		// 第 N+1 个实例必在槽位 N 成功;目录数 = 历史峰值并发数,不随启动次数累积。
	}
#ifdef DEV_ENABLED
	log_stderr(("[webview_core] init: cache path = " + cache_path + "\n").c_str());
#endif

	CefSettings settings;
	CefString(&settings.browser_subprocess_path) = subprocess_path;
	CefString(&settings.root_cache_path) = cache_path;
#if defined(__APPLE__)
	// 非 bundle 可执行文件下 CEF 的 framework bundle 定位会落到错误路径(main bundle =
	// exe 文件本身,Contents/Frameworks 不存在)→ ICU/资源加载失败。framework_dir_path
	// 是官方机制:CefInitialize 时转成 --framework-dir-path 命令行开关,util_mac::
	// BasicStartupComplete 据此 SetOverrideFrameworkBundlePath(见 libcef/common/
	// chrome_main_delegate_cef.cc)。必须为绝对路径。
	CefString(&settings.framework_dir_path) = framework_dir;
	// 同样地,BaseBundleID(mach rendezvous 服务名前缀)来自 main bundle 的
	// CFBundleIdentifier——裸可执行文件取不到,浏览器与 helper 各用各的 id 会导致
	// bootstrap_look_up 失败。main_bundle_path 指向基础 helper bundle(其 Info.plist
	// 的 bundle id 与全部 helper 统一为 com.cefview.CefViewWing,见 stage_webview.py
	// patch_helper_plists),浏览器进程即取得同一 id。
	CefString(&settings.main_bundle_path) = runtime_root + "/CefViewWing.app";
#endif
	settings.windowless_rendering_enabled = 1; // OSR 软件渲染(OnPaint)
	settings.external_message_pump = 1; // 由宿主主循环 CefDoMessageLoopWork 驱动
	settings.log_severity = LOGSEVERITY_DEFAULT; // debug.log 在 exe 目录
	// 不设 background_color：页面背景是页面自身职责（body 背景不透明 + height:100%
	// 填满视口即无透明区域）。固定 CEF 底色（曾试 #223）会与未来动态主题冲突；
	// 且页面加载完成后背景不透明，底色本就不可见（仅加载瞬态可能短暂露黑）。

	impl_->app_delegate = std::make_shared<Impl::AppDelegate>(impl_.get());
	impl_->app = new CefViewBrowserApp(CefString(), CefString(), impl_->app_delegate);

	if (!CefInitialize(main_args, settings, impl_->app, nullptr)) {
		log_stderr("[webview_core] init: CefInitialize failed (terminal state)\n");
		impl_->app = nullptr;
		impl_->app_delegate.reset();
#if defined(__APPLE__)
		if (impl_->framework_loaded) {
			cef_unload_library(); // 失败出口:与 load 对称,防 framework 驻留到进程退出
			impl_->framework_loaded = false;
		}
#endif
		impl_->cef_failed = true; // 终态
		return false;
	}

	impl_->cef_initialized = true;
	log_stderr("[webview_core] init: CEF initialized\n");
	return true;
}

void WebViewCore::pump() {
	if (impl_ == nullptr) {
		return;
	}
	if (!impl_->cef_initialized || impl_->cef_failed || impl_->cef_shutdown) {
		return;
	}

	// internal_begin_frame 模式(external_begin_frame_enabled=0):CEF 内部帧源按
	// windowless_frame_rate=60 驱动,宿主不再 SendExternalBeginFrame。
	// 每帧泵送:internal BF 的帧处理依赖 CefDoMessageLoopWork 持续运转(实测节流泵会
	// 饿死内部帧源→动画 0 帧);CEF 无工作时开销近乎为零(实测静态页 60s CEF 累计 CPU≈0)。
	// 清空 pump_requested 防标志累积(每帧泵下不再作为门控)。
	impl_->pump_requested.exchange(false, std::memory_order_relaxed);

	CefDoMessageLoopWork();
}

void WebViewCore::shutdown() {
	if (impl_ == nullptr) {
		return;
	}
	if (impl_->cef_shutdown) {
		return; // 幂等
	}

	if (impl_->cef_initialized && !impl_->cef_failed) {
		// 1. 关闭全部尚未关闭的浏览器(带活浏览器 CefShutdown 会断言/崩溃)。
		for (auto &kv : impl_->browsers) {
			Impl::BrowserEntry &entry = kv.second;
			if (!entry.closing) {
				entry.closing = true;
				impl_->pending_close++;
				entry.browser->GetHost()->CloseBrowser(true);
			}
		}

		// 2. 有界泵等待异步关闭送达 OnBeforeClose(墙钟超时,避免死等;不能只用帧计数——
		// 每帧 CefDoMessageLoopWork 可能很快,renderer 子进程回 OnBeforeClose 需要真实时间)。
		// 注意:超时后不得 CefShutdown——CEF 要求所有浏览器 OnBeforeClose 后才可关闭。
		const uint64_t kShutdownWaitMs = 3000; // 3 秒(墙钟,steady_clock)
		const uint64_t shutdown_deadline_ms = now_ms() + kShutdownWaitMs;
		while (impl_->pending_close > 0 && now_ms() < shutdown_deadline_ms) {
			CefDoMessageLoopWork();
		}

		if (impl_->pending_close > 0) {
			// 超时仍有浏览器未完成异步关闭:不 CefShutdown(会崩溃/断言),显式报错暴露问题。
			log_stderr(("[webview_core] CefShutdown aborted: pending_close=" + std::to_string(impl_->pending_close) + " (timeout " + std::to_string(kShutdownWaitMs / 1000) + "s)\n").c_str());
			impl_->cef_initialized = false;
			impl_->cef_shutdown = true;
			impl_->pump_requested.store(false, std::memory_order_relaxed);
			impl_->paint_buffer.clear();
			impl_->callbacks = Callbacks(); // 断开宿主回调,防 shutdown 后泄漏/穿越
			return;
		}

		// 3. 释放 CEF 引用(先 client 后 app:client 析构会 CheckOutClient 到 app),再 CefShutdown。
		impl_->browsers.clear();
		impl_->app = nullptr;
		impl_->app_delegate.reset();
		CefShutdown();
#if defined(__APPLE__)
		// mac:与 init 的 cef_load_library 对应,卸载 framework(仅成功初始化过才卸载)。
		if (impl_->framework_loaded) {
			cef_unload_library();
			impl_->framework_loaded = false;
		}
#endif
	}

	impl_->cef_initialized = false;
	impl_->cef_shutdown = true;
	impl_->pump_requested.store(false, std::memory_order_relaxed);
	impl_->pending_close = 0;
	impl_->paint_buffer.clear();
	impl_->callbacks = Callbacks(); // 断开宿主回调,防 shutdown 后泄漏/穿越
}

int WebViewCore::create_browser(int32_t p_id, const std::string &p_url, uint32_t p_w, uint32_t p_h) {
	if (impl_ == nullptr) {
		return -1;
	}
	if (!impl_->cef_initialized || impl_->cef_failed || impl_->cef_shutdown) {
		return -1;
	}
	if (!is_valid_browser_size(p_w, p_h)) {
		return -1;
	}
	if (impl_->browsers.count(p_id) > 0) {
		return -1;
	}

	CefWindowInfo window_info;
	window_info.windowless_rendering_enabled = 1; // OSR
	window_info.shared_texture_enabled = 0; // 软件路径 → OnPaint(BGRA)
	// internal 帧源（external_begin_frame_enabled=0）：CEF 按 windowless_frame_rate
	// 驱动。曾试 external（=1）+ 宿主持续 SendExternalBeginFrame：软件渲染路径下
	// viz 不触发 Draw（OnPaint 完全无输出，实测 8 秒卡死）——软件 OSR 不支持外部
	// 帧驱动。internal 模式下 onPaint 正常输出，resize 收敛延迟（hold 机制）由宿主
	// “尾随重发同尺寸”加速（见 web_panel NOTIFICATION_PROCESS）。
	window_info.external_begin_frame_enabled = 0;

	CefBrowserSettings browser_settings;
	browser_settings.windowless_frame_rate = 60;

	// 每个浏览器一个 client/delegate:id 与尺寸在创建前绑定(GetViewRect 直接读取)。
	auto client_delegate = std::make_shared<Impl::ClientDelegate>(impl_.get(), p_id, p_w, p_h);
	CefRefPtr<CefViewBrowserClient> client = new CefViewBrowserClient(impl_->app, client_delegate);

	CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(window_info, client, p_url, browser_settings, nullptr, nullptr);
	if (!browser) {
		return -1; // client 的 CefRefPtr 随作用域释放(CefViewBrowserClient 析构 CheckOutClient)
	}

	Impl::BrowserEntry entry;
	entry.browser = browser;
	entry.client = client;
	entry.client_delegate = client_delegate;
	entry.width = p_w;
	entry.height = p_h;
	impl_->browsers[p_id] = std::move(entry);
	return 0;
}

int WebViewCore::resize_browser(int32_t p_id, uint32_t p_w, uint32_t p_h) {
	if (impl_ == nullptr) {
		return -1;
	}
	if (!impl_->cef_initialized || impl_->cef_failed || impl_->cef_shutdown) {
		return -1;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return -1;
	}
	if (!is_valid_browser_size(p_w, p_h)) {
		return -1; // 与 create_browser 统一校验:0 与 >INT_MAX 拒绝,防 GetViewRect 空/负
	}

	Impl::BrowserEntry &entry = it->second;
	entry.width = p_w;
	entry.height = p_h;
	// 同步 delegate 内尺寸(GetViewRect 直接读取,无需跨结构查找)。
	static_cast<Impl::ClientDelegate *>(entry.client_delegate.get())->set_size(p_w, p_h);
	entry.browser->GetHost()->WasResized(); // 通知 CEF 重新查询 GetViewRect
	// Invalidate(PET_VIEW) 请求整幅重绘（cefclient OnResize 同款配套）：页面静止时
	// 合成器按需不产帧，Invalidate 输出 host_display_client 的 pixel_size_（由 viz
	// 分配共享内存时更新）——新尺寸渲染由 internal 帧源 + 宿主“尾随重发”加速收敛
	// （见 web_panel NOTIFICATION_PROCESS 与 create_browser 注释）。
	entry.browser->GetHost()->Invalidate(PET_VIEW);
	return 0;
}

int WebViewCore::navigate_browser(int32_t p_id, const std::string &p_url) {
	if (impl_ == nullptr) {
		return -1;
	}
	if (!impl_->cef_initialized || impl_->cef_failed || impl_->cef_shutdown) {
		return -1;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return -1;
	}
	CefRefPtr<CefFrame> frame = it->second.browser->GetMainFrame();
	if (!frame) {
		return -1;
	}
	frame->LoadURL(p_url);
	return 0;
}

void WebViewCore::destroy_browser(int32_t p_id) {
	if (impl_ == nullptr) {
		return;
	}
	if (!impl_->cef_initialized || impl_->cef_failed || impl_->cef_shutdown) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	Impl::BrowserEntry &entry = it->second;
	if (!entry.closing) {
		entry.closing = true;
		impl_->pending_close++;
		entry.browser->GetHost()->CloseBrowser(true); // force:跳过 unload 延迟
	}
	// 条目保留至 OnBeforeClose 回调移除(防重复 CloseBrowser)。
}

bool WebViewCore::respond_query(int32_t p_id, int64_t p_query_id, bool p_success, const std::string &p_response, int p_error) {
	if (impl_ == nullptr) {
		return false;
	}
	if (!impl_->cef_initialized || impl_->cef_failed || impl_->cef_shutdown) {
		return false;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return false;
	}
	// CefViewQueryHandler::Response:从待应答表移除 query_id 并回调 JS 侧 callback。
	return it->second.client->ResponseQuery(p_query_id, p_success, p_response, p_error);
}

// ---------------------------------------------------------------------------
// 输入事件转发:OSR 无原生窗口,宿主把鼠标/键盘/焦点事件转发给 CEF。
// 全部调用必须在主线程(与 pump 同线程);失败路径静默返回(与其它转发 API 一致)。
// ---------------------------------------------------------------------------

void WebViewCore::send_mouse_move(int32_t p_id, int p_x, int p_y, uint32_t p_modifiers, bool p_leave) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	CefMouseEvent ev;
	ev.x = p_x;
	ev.y = p_y;
	ev.modifiers = p_modifiers;
	it->second.browser->GetHost()->SendMouseMoveEvent(ev, p_leave);
}

void WebViewCore::send_mouse_click(int32_t p_id, int p_x, int p_y, uint32_t p_modifiers, int p_button, bool p_up, int p_click_count) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	if (p_button < MOUSE_LEFT || p_button > MOUSE_RIGHT) {
		return;
	}
	CefMouseEvent ev;
	ev.x = p_x;
	ev.y = p_y;
	ev.modifiers = p_modifiers;
	it->second.browser->GetHost()->SendMouseClickEvent(ev, static_cast<cef_mouse_button_type_t>(p_button), p_up, p_click_count);
}

void WebViewCore::send_mouse_wheel(int32_t p_id, int p_x, int p_y, uint32_t p_modifiers, int p_delta_x, int p_delta_y) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	CefMouseEvent ev;
	ev.x = p_x;
	ev.y = p_y;
	ev.modifiers = p_modifiers;
	it->second.browser->GetHost()->SendMouseWheelEvent(ev, p_delta_x, p_delta_y);
}

void WebViewCore::send_key_event(int32_t p_id, int p_type, uint32_t p_modifiers, int p_windows_key_code, int p_native_key_code, uint32_t p_character, uint32_t p_unmodified_character, bool p_focus_on_editable) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	if (p_type < KEY_RAWKEYDOWN || p_type > KEY_CHAR) {
		return;
	}
	CefKeyEvent ev;
	ev.type = static_cast<cef_key_event_type_t>(p_type);
	ev.modifiers = p_modifiers;
	ev.windows_key_code = p_windows_key_code;
	ev.native_key_code = p_native_key_code;
	ev.is_system_key = 0;
	ev.character = static_cast<char16_t>(p_character);
	ev.unmodified_character = static_cast<char16_t>(p_unmodified_character);
	// 可编辑焦点状态由 CEF 回调(focusedEditableNodeChanged)按浏览器权威提供,面板传入参数仅作后备。
	ev.focus_on_editable_field = (it->second.focus_on_editable || p_focus_on_editable) ? 1 : 0;
	CefRefPtr<CefBrowser> browser = it->second.browser;

	// 补充平面字符(U+10000..U+10FFFF):CEF 字符字段是 UTF-16 code unit,必须拆代理对
	// 发两次 CHAR 事件,否则 char16_t 截断产生错误字符(如 emoji U+1F600 → U+F600)。
	if (p_type == KEY_CHAR && p_character >= 0x10000 && p_character <= 0x10FFFF) {
		const uint32_t v = p_character - 0x10000;
		const char16_t hi = static_cast<char16_t>(0xD800 + (v >> 10));
		const char16_t lo = static_cast<char16_t>(0xDC00 + (v & 0x3FF));
		CefKeyEvent ev_hi = ev;
		ev_hi.character = hi;
		ev_hi.unmodified_character = static_cast<char16_t>(p_unmodified_character >= 0x10000 && p_unmodified_character <= 0x10FFFF ? (0xD800 + ((p_unmodified_character - 0x10000) >> 10)) : p_unmodified_character);
		// Windows OSR 的 CHAR 路径用 windows_key_code 作字符载荷——代理对必须对应 hi/lo 值。
		ev_hi.windows_key_code = hi;
		browser->GetHost()->SendKeyEvent(ev_hi);
		CefKeyEvent ev_lo = ev;
		ev_lo.character = lo;
		ev_lo.unmodified_character = static_cast<char16_t>(p_unmodified_character >= 0x10000 && p_unmodified_character <= 0x10FFFF ? (0xDC00 + ((p_unmodified_character - 0x10000) & 0x3FF)) : p_unmodified_character);
		ev_lo.windows_key_code = lo;
		browser->GetHost()->SendKeyEvent(ev_lo);
		return;
	}
	browser->GetHost()->SendKeyEvent(ev);
}

void WebViewCore::set_focus(int32_t p_id, bool p_focus) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	it->second.browser->GetHost()->SetFocus(p_focus);
}

bool WebViewCore::emit_event(int32_t p_id, const std::string &p_event_name, const std::vector<std::string> &p_args) {
	if (!is_initialized()) {
		return false;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return false;
	}
	// 事件下行(协议层:addEventListener 的事件名 + 字符串参数列表)。
	// renderer 侧 CefViewRenderApp::OnTriggerEventNotifyMessage 按事件名分发到 JS 监听器。
	CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create(kCefViewClientBrowserTriggerEventMessage);
	CefRefPtr<CefListValue> args = msg->GetArgumentList();
	args->SetString(0, p_event_name);
	for (size_t i = 0; i < p_args.size(); i++) {
		args->SetString(static_cast<int>(i + 1), p_args[i]);
	}
	return it->second.client->TriggerEvent(it->second.browser, CEFVIEW_MAIN_FRAME, msg);
}

void WebViewCore::ime_set_composition(int32_t p_id, const std::string &p_text, uint32_t p_selection_start, uint32_t p_selection_end) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	std::vector<CefCompositionUnderline> underlines; // 基础版:无下划线(候选窗定位为完整版)
	// "无替换范围"必须用 InvalidRange:Chromium 151 视零宽 [0,0) 为合法 range,会先
	// SelectRange(0,0) 把 caret 强制移到文档偏移 0——组合/上屏文本跑到文本开头
	// (shifu 源码级确认;cefclient osr_window_win.cc 同用 InvalidRange)。
	const CefRange replacement = CefRange::InvalidRange();
	const CefRange selection(p_selection_start, p_selection_end);
	it->second.browser->GetHost()->ImeSetComposition(p_text, underlines, replacement, selection);
}

void WebViewCore::ime_commit_text(int32_t p_id, const std::string &p_text) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	const CefRange replacement = CefRange::InvalidRange();
	it->second.browser->GetHost()->ImeCommitText(p_text, replacement, 0);
}

void WebViewCore::ime_cancel_composition(int32_t p_id) {
	if (!is_initialized()) {
		return;
	}
	auto it = impl_->browsers.find(p_id);
	if (it == impl_->browsers.end()) {
		return;
	}
	it->second.browser->GetHost()->ImeCancelComposition();
}

void WebViewCore::set_callbacks(const Callbacks &p_callbacks) {
	if (impl_ == nullptr) {
		return;
	}
	impl_->callbacks = p_callbacks;
}

void WebViewCore::set_log_callback(LogCallback p_cb) {
	s_log_cb = std::move(p_cb);
}

bool WebViewCore::is_initialized() const {
	return impl_ != nullptr && impl_->cef_initialized && !impl_->cef_failed && !impl_->cef_shutdown;
}
