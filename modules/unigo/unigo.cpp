/**************************************************************************/
/*  unigo.cpp                                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         UNIGO ENGINE (Fork)                            */
/*                    https://github.com/misutime/unigo-godot-kernel      */
/**************************************************************************/
/* UniGo C ABI 实现:封装 GodotInstance + Main,向宿主(C# EditorHost)提供 */
/* 稳定的 初始化 / tick / shutdown 入口。本层是 UniGo 与 Godot 的唯一     */
/* 原生边界,宿主只见 Handle/POD 与错误码,不接触任何 Godot 类型。         */
/*                                                                        */
/* 生命周期序列:                                                          */
/*   create  → libgodot_create_godot_instance(Main::setup + initialize)   */
/*             + GodotInstance::start()(Main::setup2 + Main::start)       */
/*   iterate → GodotInstance::iteration()(process_events + Main::iteration)*/
/*   shutdown→ GodotInstance::stop() + libgodot_destroy_godot_instance    */
/*             (main_loop finalize + Main::cleanup)                       */
/**************************************************************************/

#include "unigo.h"

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/os/os.h"
#include "main/main.h"

#include <atomic>
#include <mutex>
#include <string.h>

#if defined(EDITOR_NATIVE_DLL) && defined(WINDOWS_ENABLED)
#include <windows.h>
/* 引擎主窗 HWND getter(定义于 platform/windows/display_server_windows.cpp)。 */
extern "C" void *editor_native_query_engine_hwnd(void);
#endif

/* ---- 最小 GDExtension 初始化函数 ---- */
/* libgodot 强制要求宿主提供一个 GDExtension 初始化函数(内部经它注册伪  */
/* GDExtension 并走完整引擎初始化)。UniGo 不是 GDExtension,不注册任何    */
/* 类,因此只提供最小合法实现:设置最低初始化等级,回调为空函数体          */
/* (GDExtension 要求 initialize/deinitialize 非空)。                      */
static void unigo_minimal_extension_initialize(void *p_userdata, GDExtensionInitializationLevel p_level) {
	(void)p_userdata;
	(void)p_level;
	/* UniGo C ABI 不依赖 GDExtension 类注册,无初始化动作。 */
}

static void unigo_minimal_extension_deinitialize(void *p_userdata, GDExtensionInitializationLevel p_level) {
	(void)p_userdata;
	(void)p_level;
	/* 同上,无清理动作。 */
}

static GDExtensionBool unigo_minimal_extension_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	(void)p_get_proc_address;
	(void)p_library;
	if (r_initialization == nullptr) {
		return 0; /* 非法参数:结构指针为空。 */
	}
	r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_CORE;
	r_initialization->userdata = nullptr;
	r_initialization->initialize = unigo_minimal_extension_initialize;
	r_initialization->deinitialize = unigo_minimal_extension_deinitialize;
	return 1; /* 成功。 */
}

/* ---- 内部状态:一个 Godot 内核实例 + 退出标志 ---- */
struct UnigoEngineState {
	GodotInstance *instance; /* 内核实例(libgodot create 返回的 GodotInstance) */
	std::atomic<bool> requested_exit; /* 宿主请求退出标志(幂等置位) */
	std::atomic<bool> shutdown_done; /* 已销毁标志(幂等保护) */
	std::mutex instance_mutex; /* 防止迭代与销毁并发访问内核实例 */
};

/* ---- 线程局部错误缓冲(每次调用后写入,供 unigo_last_error 读取) ---- */
static thread_local char s_last_error[512];
static thread_local bool s_has_error = false;

/* 记录诊断字符串(截断到缓冲上限)。 */
static void unigo_set_error(const char *p_msg) {
	if (p_msg == nullptr) {
		s_has_error = false;
		s_last_error[0] = '\0';
		return;
	}
	strncpy_s(s_last_error, sizeof(s_last_error), p_msg, _TRUNCATE);
	s_has_error = true;
}

extern "C" {

/* MSVC 下强制导出这些 C ABI 符号(链接器级白名单)。 */
/* 说明:跨 TU 的 __declspec(dllexport) 声明在部分构建形态下不传播到定义, */
/* 因此这里用 #pragma comment(linker, "/export:...") 明确要求链接器导出, */
/* 与 libgodot.h 的 EDITOR_NATIVE_DLL "导出闭环" 思路一致(只导出 UniGo 需要的)。 */
#ifdef _MSC_VER
#pragma comment(linker, "/export:unigo_engine_create")
#pragma comment(linker, "/export:unigo_engine_iterate")
#pragma comment(linker, "/export:unigo_engine_request_exit")
#pragma comment(linker, "/export:unigo_engine_shutdown")
#pragma comment(linker, "/export:unigo_engine_query_render_support")
#pragma comment(linker, "/export:unigo_engine_ensure_view_top")
#pragma comment(linker, "/export:unigo_last_error")
#endif

/* ---- create:创建并启动 Godot 内核 ---- */
UNIGO_API unigo_handle unigo_engine_create(const unigo_config *p_cfg) {
	unigo_set_error(nullptr);

	/* 参数校验:execpath 是 Main::setup 的硬性要求,缺失直接失败。 */
	if (p_cfg == nullptr || p_cfg->execpath == nullptr) {
		unigo_set_error("unigo_engine_create: 缺少 execpath");
		return nullptr;
	}

	/* 构造 argv:优先用宿主提供的 argv[0],否则以 execpath 作为 argv[0]。 */
	/* project_path 转成 --path 参数并插在 argv[0] 后,其余宿主参数保持原序。 */
	/* parent_hwnd 非 0 时注入 Godot 官方 --wid 嵌入参数(创建即 WS_CHILD 子窗)。 */
	/* 上限 16 是第一阶段保护(冒烟场景参数极少),超出部分按尾部参数截断。 */
	const char *argv_buf[16];
	const bool has_host_argv = p_cfg->argc > 0 && p_cfg->argv != nullptr;
	const int host_argc = has_host_argv ? p_cfg->argc : 0;
	/* 嵌入参数占位(--wid <num> --position 0,0 --resolution 320x240 共 6 个)。 */
	const int embed_argc = p_cfg->parent_hwnd != 0 ? 6 : 0;
	int argc = 1;
	argv_buf[0] = has_host_argv && p_cfg->argv[0] != nullptr ? p_cfg->argv[0] : p_cfg->execpath;
	if (p_cfg->project_path != nullptr && p_cfg->project_path[0] != '\0') {
		argv_buf[argc++] = "--path";
		argv_buf[argc++] = p_cfg->project_path;
	}
	char wid_str[32];
	if (embed_argc > 0) {
		/* --wid 用十进制(to_int 不认 0x 前缀);给定位置/尺寸避免子窗在客户区外。 */
		_snprintf_s(wid_str, sizeof(wid_str), _TRUNCATE, "%llu", (unsigned long long)p_cfg->parent_hwnd);
		argv_buf[argc++] = "--wid";
		argv_buf[argc++] = wid_str;
		argv_buf[argc++] = "--position";
		argv_buf[argc++] = "0,0";
		argv_buf[argc++] = "--resolution";
		argv_buf[argc++] = "320x240";
	}
	for (int i = has_host_argv ? 1 : 0; i < host_argc && argc < 16; i++) {
		argv_buf[argc++] = p_cfg->argv[i];
	}
	char *argv_ptrs[16];
	for (int i = 0; i < argc; i++) {
		argv_ptrs[i] = const_cast<char *>(argv_buf[i]);
	}

	/* libgodot create:内部 Main::setup + initialize(含 OS_Windows 模块句柄处理)。 */
	/* 返回 GodotInstance(经 GDExtensionObjectPtr 伪装);失败返回 nullptr。 */
	/* init_func 必须非空(libgodot 强制要求),提供最小 GDExtension init。 */
	GodotInstance *instance = (GodotInstance *)libgodot_create_godot_instance(argc, argv_ptrs, unigo_minimal_extension_init);
	if (instance == nullptr) {
		unigo_set_error("unigo_engine_create: libgodot_create_godot_instance 失败(Main::setup 或 initialize)");
		return nullptr;
	}

	/* start:Main::setup2 + Main::start + main_loop initialize(引擎真正进入可迭代状态)。 */
	if (!instance->start()) {
		libgodot_destroy_godot_instance((GDExtensionObjectPtr)instance);
		unigo_set_error("unigo_engine_create: GodotInstance::start 失败(Main::setup2/start)");
		return nullptr;
	}

	UnigoEngineState *state = new UnigoEngineState();
	state->instance = instance;
	state->requested_exit.store(false);
	state->shutdown_done.store(false);

	return (unigo_handle)state;
}

/* ---- iterate:驱动一帧 ---- */
UNIGO_API int32_t unigo_engine_iterate(unigo_handle p_handle) {
	unigo_set_error(nullptr);

	if (p_handle == nullptr) {
		unigo_set_error("unigo_engine_iterate: 句柄为空");
		return -UNIGO_ERR_INVALID_ARG;
	}

	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	std::lock_guard<std::mutex> lock(state->instance_mutex);
	if (state->shutdown_done.load()) {
		unigo_set_error("unigo_engine_iterate: 句柄已进入 shutdown 状态");
		return -UNIGO_ERR_SHUTDOWN;
	}
	if (state->instance == nullptr) {
		unigo_set_error("unigo_engine_iterate: 句柄无有效内核实例");
		return -UNIGO_ERR_INVALID_HANDLE;
	}

	/* 退出标志已置位:返回 1 让宿主结束循环(幂等语义,与 request_exit 呼应)。 */
	if (state->requested_exit.load()) {
		return 1;
	}

	/* GodotInstance::iteration 返回 true 表示请求退出,false 表示继续运行。 */
	if (state->instance->iteration()) {
		state->requested_exit.store(true);
		return 1;
	}

	return 0; /* 继续运行。 */
}

/* ---- request_exit:幂等置位退出标志 ---- */
UNIGO_API int32_t unigo_engine_request_exit(unigo_handle p_handle) {
	unigo_set_error(nullptr);

	if (p_handle == nullptr) {
		unigo_set_error("unigo_engine_request_exit: 句柄为空");
		return UNIGO_ERR_INVALID_ARG;
	}

	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	if (state->shutdown_done.load()) {
		unigo_set_error("unigo_engine_request_exit: 句柄已进入 shutdown 状态");
		return UNIGO_ERR_SHUTDOWN;
	}

	state->requested_exit.store(true);
	return UNIGO_OK;
}

/* ---- shutdown:销毁内核(幂等) ---- */
UNIGO_API void unigo_engine_shutdown(unigo_handle p_handle) {
	if (p_handle == nullptr) {
		return;
	}

	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	std::lock_guard<std::mutex> lock(state->instance_mutex);
	if (state->shutdown_done.exchange(true)) {
		return; /* 幂等:内核已销毁。 */
	}

	if (state->instance != nullptr) {
		state->instance->stop();
		libgodot_destroy_godot_instance((GDExtensionObjectPtr)state->instance);
		state->instance = nullptr;
	}

	/* C ABI 没有独立的句柄释放入口,保留轻量状态作为 shutdown 后的安全墓碑。 */
}

/* ---- ensure_view_top:嵌入 Z-order 自愈(EditorHost 每帧调用) ---- */
/* 背景(定稿方案 §4 坑1/坑2):Engine 子窗创建即被 Chromium 合成层
 * (Intermediate D3D Window / RenderWidgetHostHWND)压住;Chromium 在启动/恢复/
 * resize 时会把合成子窗重排到上方。宿主每帧调用本函数,把 Engine 子窗提升到
 * 父窗口 Z-order 顶部(HWND_TOP,不抢焦点、不跨应用置顶)。 */
UNIGO_API int32_t unigo_engine_ensure_view_top(unigo_handle p_handle) {
	if (p_handle == nullptr) {
		unigo_set_error("unigo_engine_ensure_view_top: 句柄为空");
		return -UNIGO_ERR_INVALID_ARG;
	}

	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	std::lock_guard<std::mutex> lock(state->instance_mutex);
	if (state->shutdown_done.load() || state->instance == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}

#if defined(EDITOR_NATIVE_DLL) && defined(WINDOWS_ENABLED)
	HWND engine_hwnd = (HWND)editor_native_query_engine_hwnd();
	if (engine_hwnd == nullptr) {
		return UNIGO_OK; /* 主窗未创建(首帧前),非错误。 */
	}

	HWND parent = GetParent(engine_hwnd);
	if (parent == nullptr) {
		return UNIGO_OK; /* 非嵌入模式(独立窗口),无需自愈。 */
	}

	/* 判顶:父窗口的首个子窗是否就是 Engine;不是则提升。 */
	HWND top_child = GetWindow(parent, GW_CHILD);
	if (top_child != engine_hwnd) {
		SetWindowPos(engine_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
#endif

	return UNIGO_OK;
}

/* ---- query_render_support:窗口渲染支持探测 ---- */
UNIGO_API int32_t unigo_engine_query_render_support(void) {
	unigo_set_error(nullptr);

	/* 第一阶段:Windows 桌面默认有图形环境,固定返回支持。 */
	/* 后续接入 DisplayServer 真实探测(无图形会话等场景)。 */
	return 1;
}

/* ---- last_error:取线程局部诊断 ---- */
UNIGO_API const char *unigo_last_error(void) {
	return s_has_error ? s_last_error : nullptr;
}

} // extern "C"
