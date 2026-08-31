// SPDX-License-Identifier: MIT
// EditorNative.cpp —— M2-2 EditorNative.dll wrapper v2（S1a1 最小启动版）
//
// 实现：AbiV1 静态单例表 + engine_create/engine_destroy（导出仅此二）+ 真实生命周期：
//   engine_create → libgodot_create_godot_instance（Main::setup）→ GodotInstance::start（setup2+Main::start）
//   tick → GodotInstance::iteration（process_events + Main::iteration）
//   shutdown → GodotInstance::stop；destroy → 原 tombstone 时序 + libgodot_destroy_godot_instance（Main::cleanup）
// 宿主扩展：提供最小 GDExtension 初始化函数（libgodot://main 需要）。
// 已知（S1a1 后续项，如实标注）：libgodot_windows.cpp 的 OS_Windows 句柄为宿主 exe、且 destroy 不删 OS——
// 由模块句柄改造（S1a1.1）与 lifecycle ledger（S1a1.2 后续）修复；本版先验证"进程内初始化 + 主循环"。

#include <cstdio>
#include <windows.h>

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "main/main.h"
#include "servers/display/display_server.h"

static char *kDefaultArgv[] = {
	(char *)"editor-native",
	(char *)"--path",
	(char *)"D:\\misutime\\104_game\\baize-godot\\.tmp\\editor-mini-project",
};

// ---- 最小宿主扩展（libgodot://main） ----
static void native_ext_initialize_cb(void *p_userdata, GDExtensionInitializationLevel p_level) {}
static void native_ext_deinitialize_cb(void *p_userdata, GDExtensionInitializationLevel p_level) {}

static GDExtensionBool native_extension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	if (r_initialization) {
		r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_CORE;
		r_initialization->userdata = nullptr;
		r_initialization->initialize = native_ext_initialize_cb;
		r_initialization->deinitialize = native_ext_deinitialize_cb;
	}
	return true;
}

// ---- AbiV1 ----
extern "C" {
typedef struct EditorNativeError {
	uint32_t code; /* 0=OK 1=EINVAL 2=EBUSY 3=EALREADY 4=EFAULT */
	char message[256];
} EditorNativeError;

typedef void *(__cdecl *Fn_AttachView)(void *ctx, void *parent_hwnd, uint64_t token, EditorNativeError *e);
typedef void (__cdecl *Fn_DetachView)(void *ctx, uint64_t token, EditorNativeError *e);
typedef void (__cdecl *Fn_Resize)(void *ctx, uint32_t w_px, uint32_t h_px, EditorNativeError *e);
typedef void (__cdecl *Fn_Tick)(void *ctx, EditorNativeError *e);
typedef void (__cdecl *Fn_SetFps)(void *ctx, uint32_t fps, EditorNativeError *e);
typedef void (__cdecl *Fn_Shutdown)(void *ctx, EditorNativeError *e);
typedef void *(__cdecl *Fn_Canary)(void *ctx);

typedef struct EditorNativeAbiV1 {
	uint32_t size;
	uint32_t version; /* 1 */
	volatile uint32_t busy;
	uint32_t freed;
	void *ctx; /* GodotInstance* */
	Fn_AttachView attach_view;
	Fn_DetachView detach_view;
	Fn_Resize resize;
	Fn_Tick tick;
	Fn_SetFps set_fps;
	Fn_Shutdown shutdown;
	Fn_Canary canary_module_handle;
} EditorNativeAbiV1;
} // extern "C"

static EditorNativeAbiV1 g_abi{};
static GodotInstance *g_godot = nullptr;
static bool g_started = false;
static bool g_start_failed = false; // 熔断：start() 失败后不再重试（二次 setup2 会重复注册单例/窗口类导致连锁崩）
static HWND g_engine_hwnd = nullptr;
static HWND g_parking_hwnd = nullptr;
static bool g_attached = false;
static HWND g_attach_parent = nullptr;
static HWND g_host_parent = nullptr;
static bool g_embedded_init = false;
static HWND g_walk_result = nullptr;

static GodotInstance *ctx_instance(void *ctx) {
	return (GodotInstance *)ctx;
}

static HWND engine_window_handle() {
	// engine main window HWND via pure Win32 enumeration；优先按类名 "Engine"（Godot 注册的窗口类）
	// 精确匹配——兼容顶层（--no-cross-process/P0-1 早期）与 WS_CHILD 子窗口（--wid 创建即嵌入）两种形态。
	// 旧实现只 EnumWindows（顶层），--wid 子窗模式找不到主窗口会误抓到其它顶层窗。
	if (g_engine_hwnd) {
		return g_engine_hwnd;
	}
	DWORD tid = GetCurrentThreadId();
	std::printf("[EditorNative] ewh: enum windows by class \"Engine\" (tid=%lu)\n", tid); std::fflush(stdout);
	g_walk_result = nullptr;
	// 全桌面遍历顶层 + 一层子窗口，找类名 == "Engine"（Godot 专用类名，跨进程/子窗形态均可命中）。
	// 不能做线程过滤：--wid 嵌入时 Engine 是 Electron 进程（unigo）的子窗口，EnumWindows 只给顶层，
	// 必须进它的子窗口搜。
	EnumWindows([](HWND h, LPARAM) -> BOOL {
		wchar_t cls[64] = {};
		if (GetClassNameW(h, cls, 64) && wcscmp(cls, L"Engine") == 0) {
			g_walk_result = h;
			return FALSE;
		}
		EnumChildWindows(h, [](HWND ch, LPARAM) -> BOOL {
			wchar_t ccls[64] = {};
			if (GetClassNameW(ch, ccls, 64) && wcscmp(ccls, L"Engine") == 0) {
				g_walk_result = ch;
				return FALSE;
			}
			return TRUE;
		}, 0);
		return g_walk_result == nullptr;
	}, 0);
	g_engine_hwnd = g_walk_result;
	std::printf("[EditorNative] ewh: found=%p\n", (void *)g_engine_hwnd); std::fflush(stdout);
	return g_engine_hwnd;
}

// 嵌入视口可见性修复（P0-2 关键）:
// Godot 以 --wid 创建即子窗口时，新窗口默认排在父窗口子窗口 Z-order 底部，
// 会被 Chromium 的合成子窗（Intermediate D3D Window / Chrome_RenderWidgetHostHWND）盖住→不可见。
// SetParent 旧路径天然到顶（SetParent 语义），故 P0-1 可见；创建即子窗路径必须主动提升。
// 每次迭代检查引擎窗口是否为父窗口子窗口 Z-order 顶部；不是则 SetWindowPos(HWND_TOP)。
// 不缓存“已确认过”：Electron 在最小化/恢复/resize/GPU 进程重建时会重排子窗口（实测 RenderWidgetHostHWND
// 会被重新排到顶部），必须每迭代复查才能真正自愈。GetWindow(GW_CHILD) 为单次轻量调用，无性能顾虑。
static void ensure_engine_z_top() {
	if (!g_started) { return; }
	HWND w = engine_window_handle();
	if (!w || !IsWindow(w)) { return; }
	HWND par = GetParent(w);
	if (!par || par == g_parking_hwnd) { return; } // 顶层独立窗口（非嵌入）不动；parking 阶段也不动
	HWND first = GetWindow(par, GW_CHILD);
	if (first != w) {
		SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		std::printf("[EditorNative] ztop: engine -> HWND_TOP under parent=%p\n", (void *)par); std::fflush(stdout);
	}
}static void *__cdecl attach_impl(void *ctx, void *parent, uint64_t token, EditorNativeError *e) {
	std::printf("[EditorNative] attach: enter\n"); std::fflush(stdout);
	HWND child = engine_window_handle();
	if (e) { e->code = 0; }
	HWND parent_h = (HWND)parent;
	std::printf("[EditorNative] attach: child=%p parent=%p iswin=%d\n", (void *)child, (void *)parent_h, child ? (int)IsWindow(child) : -1); std::fflush(stdout);
	if (!child || !parent_h || IsWindow(parent_h) == FALSE || IsWindow(child) == FALSE) {
		if (e) { e->code = 1; }
		return nullptr;
	}
	// attach 语义 = 强制挂到指定父（不依赖 g_attached）：detach 到 parking 后 g_attached 仍为 true，
	// 若用 !g_attached 守卫则 attach 永远不会把窗口从 parking 拉回（P0-2 attach/detach 循环实测 code=1）。
	{
		LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
		style &= ~WS_POPUP;
		style |= WS_CHILD | WS_CLIPSIBLINGS;
		SetWindowLongPtrW(child, GWL_STYLE, style);
		HWND sp = SetParent(child, parent_h);
		std::printf("[EditorNative] attach: SetParent hwnd=%p\n", (void *)sp); std::fflush(stdout);
		SetWindowPos(child, nullptr, 0, 0, 320, 240, SWP_NOACTIVATE | SWP_NOZORDER);
	}
	if (GetParent(child) == parent_h) {
		g_attached = true;
		if (e) { e->code = 0; }
		return (void *)child;
	}
	if (e) { e->code = 1; }
	return nullptr;
}

static void __cdecl detach_impl(void *ctx, uint64_t token, EditorNativeError *e) {
	if (e) { e->code = 0; }
	HWND child = engine_window_handle();
	if (child && g_parking_hwnd) {
		SetParent(child, g_parking_hwnd);
		SetWindowPos(child, nullptr, 0, 0, 320, 240, SWP_NOACTIVATE | SWP_NOZORDER);
	}
	g_attached = GetParent(child) == g_parking_hwnd;
	if (!g_attached && e) { e->code = 1; }
}

static void __cdecl resize_impl(void *ctx, uint32_t w, uint32_t h, EditorNativeError *e) {
	if (e) { e->code = 0; }
	HWND child = engine_window_handle();
	if (child) {
		// 固定面板客户区左上 + 面板尺寸（SWP_NOMOVE 保留 0,0；精确跟随避免子窗边界与面板不齐导致光标闪）
		SetWindowPos(child, nullptr, 0, 0, (int)w, (int)h, SWP_NOACTIVATE | SWP_NOZORDER);
	}
}
static void __cdecl stub_detach(void *ctx, uint64_t token, EditorNativeError *e) {
	if (e) { e->code = 1; }
}
static void __cdecl stub_resize(void *ctx, uint32_t w, uint32_t h, EditorNativeError *e) {
	if (e) { e->code = 1; }
}
static void __cdecl tick_impl(void *ctx, EditorNativeError *e) {
	if (!g_godot) { if (e) { e->code = 1; } return; }
	// 熔断：start() 曾失败——不能再进 setup2 重试（会二次注册单例/窗口类→“Failed to register window class”等连环错）
	if (g_start_failed) {
		if (e) { e->code = 5; } // 5=ESTARTFAILED（自定义扩展码：start 失败已熔断）
		return;
	}
	// 泵热身（start 前/后都推进窗口消息；窗口模式初始化依赖消息泵）
	MSG msg;
	for (int pump = 0; pump < 64 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); pump++) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	// lazy start（不阻塞 create；窗口模式渲染初始化由引擎线程消息泵驱动）
	if (!g_started) {
		// 只允许尝试一次；失败即熔断，不再重试
		g_started = g_godot->start();
		if (!g_started) {
			g_start_failed = true;
			std::printf("[EditorNative] start FAILED (scene/init error); engine latched, further ticks return ESTARTFAILED\n"); std::fflush(stdout);
			if (e) { e->code = 5; }
			return;
		}
		if (g_started && !g_embedded_init && g_host_parent) {
			HWND w = engine_window_handle();
			if (w && IsWindow(w)) {
				LONG_PTR st = GetWindowLongPtrW(w, GWL_STYLE);
				// 装饰清零：不保留标题栏/边框/系统菜单（融合为面板内容区）
				st &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
				st |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
				SetWindowLongPtrW(w, GWL_STYLE, st);
				SetParent(w, g_host_parent);
				SetWindowPos(w, nullptr, 0, 0, 320, 240, SWP_NOACTIVATE | SWP_NOZORDER);
				std::printf("[EditorNative] embedded-init parent=%p ok\n", (void *)g_host_parent); std::fflush(stdout);
			}
			g_embedded_init = true;
		}
	}
	if (g_started) {
		for (int pump = 0; pump < 64 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); pump++) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		g_godot->iteration();
		ensure_engine_z_top();
	}
	if (e) { e->code = 0; }
}
static void __cdecl stub_setfps(void *ctx, uint32_t fps, EditorNativeError *e) {
	if (e) { e->code = 1; }
}
static void __cdecl shutdown_impl(void *ctx, EditorNativeError *e) {
	if (g_godot) {
		g_godot->stop();
	}
	if (e) { e->code = 0; }
}
static void *__cdecl stub_canary(void *ctx) {
	HMODULE h = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&g_abi, &h);
	return h;
}

extern "C" __declspec(dllexport) EditorNativeAbiV1 *__cdecl engine_create(int argc, char **argv, EditorNativeError *err) {
	if (g_abi.version != 0) {
		if (err) { err->code = 3; } // EALREADY
		return nullptr;
	}
	if (g_godot == nullptr) {
		int aargc = (argc > 0 && argv != nullptr) ? argc : 3;
		char **aargv = (argc > 0 && argv != nullptr) ? argv : kDefaultArgv;
			// --parent-window <hex>（P0-1 旧路径：EditorNative 在 start 后自行 SetParent）
			// 注：P0-2 走 Godot 原生 --wid 时，Godot 创建即嵌入，EditorNative 无需再 SetParent，故不解析它。
	for (int i = 0; aargv && i < aargc - 1; i++) {
		if (strcmp(aargv[i], "--parent-window") == 0) {
			g_host_parent = (HWND)strtoull(aargv[i + 1], nullptr, 16);
			break;
		}
	}std::printf("[EditorNative] create: before libgodot_create\n"); std::fflush(stdout);
		g_godot = (GodotInstance *)libgodot_create_godot_instance(aargc, aargv, native_extension_init);
		std::printf("[EditorNative] create: after libgodot_create (godot=%p)\n", (void *)g_godot); std::fflush(stdout);
		if (g_godot == nullptr) {
			if (err) { err->code = 4; } // EFAULT（创建失败）
			return nullptr;
		}
		std::printf("[EditorNative] create: before start\n"); std::fflush(stdout);
			// lazy start: first tick starts engine
		std::printf("[EditorNative] create: lazy start (first tick)\n"); std::fflush(stdout);
	}
	// S1b：parking 隐藏窗口（detach 目标；engine thread 创建）。
	if (g_parking_hwnd == nullptr) {
		g_parking_hwnd = CreateWindowExW(0, L"STATIC", L"EditorNativeParking", WS_OVERLAPPED, 0, 0, 320, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	}
	g_abi.size = sizeof(EditorNativeAbiV1);
	g_abi.version = 1;
	g_abi.busy = 0;
	g_abi.freed = 0;
	g_abi.ctx = g_godot;
	g_abi.attach_view = attach_impl;
	g_abi.detach_view = detach_impl;
	g_abi.resize = resize_impl;
	g_abi.tick = tick_impl;
	g_abi.set_fps = stub_setfps;
	g_abi.shutdown = shutdown_impl;
	g_abi.canary_module_handle = stub_canary;
	if (err) { err->code = 0; }
	return &g_abi;
}

extern "C" __declspec(dllexport) void __cdecl engine_destroy(EditorNativeAbiV1 *abi, EditorNativeError *err) {
	if (abi != &g_abi) {
		if (err) { err->code = 4; }
		return;
	}
	uint32_t expected = 0;
	if (!InterlockedCompareExchange((volatile LONG *)&g_abi.busy, 1, 0)) {
		if (g_abi.freed) {
			g_abi.busy = 0;
			if (err) { err->code = 3; }
			return;
		}
		// 释放 Godot 实例（stop + memdelete + Main::cleanup；OS_Windows 释放待 S1a1.1 模块句柄改造）。
		if (g_godot) {
			libgodot_destroy_godot_instance(g_godot);
			g_godot = nullptr;
		}
		// 复位生命周期状态，支持 destroy→create 循环
		g_started = false;
		g_start_failed = false;
		g_embedded_init = false;
		g_engine_hwnd = nullptr;
		g_walk_result = nullptr;
		g_attached = false;
		g_host_parent = nullptr;
		g_abi.freed = 1;
		g_abi.busy = 0;
		if (err) { err->code = 0; }
		return;
	}
	if (err) { err->code = 2; }
}