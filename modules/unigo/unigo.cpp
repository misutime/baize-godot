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

/* C# 驱动 RenderingServer 渲染(C ABI 命令缓冲)所需头文件。 */
#include "core/math/transform_3d.h"
#include "core/variant/array.h"
#include "core/variant/variant.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/display/display_server.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string.h>

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

/* 运行时接口的公共生命周期入口:校验 handle 有效、内核未 shutdown,
 * 并在锁内返回 state(调用方持锁访问单例——与 shutdown 销毁互斥)。
 * 返回 nullptr = 无效(已置诊断,错误码 -UNIGO_ERR_SHUTDOWN)。 */
static UnigoEngineState *unigo_lock_state(unigo_handle p_handle) {
	if (p_handle == nullptr) {
		unigo_set_error("unigo 运行时接口: 句柄为空");
		return nullptr;
	}
	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	state->instance_mutex.lock();
	if (state->shutdown_done.load() || state->instance == nullptr) {
		state->instance_mutex.unlock();
		unigo_set_error("unigo 运行时接口: 内核已 shutdown");
		return nullptr;
	}
	return state; /* 调用方负责 state->instance_mutex.unlock() */
}

/* 运行时接口须在引擎线程调用(与 unigo_engine_iterate 同线程):
 * 它们操作 SceneTree/DisplayServer 等主线程单例,Viewport 等方法带
 * ERR_MAIN_THREAD_GUARD(非主线程调试构建会拒绝)。宿主主循环单线程满足。 */


extern "C" {

/* MSVC 下强制导出这些 C ABI 符号(链接器级白名单)。 */
/* 说明:跨 TU 的 __declspec(dllexport) 声明在部分构建形态下不传播到定义, */
/* 因此这里用 #pragma comment(linker, "/export:...") 明确要求链接器导出。 */
#ifdef _MSC_VER
#pragma comment(linker, "/export:unigo_engine_create")
#pragma comment(linker, "/export:unigo_engine_iterate")
#pragma comment(linker, "/export:unigo_engine_request_exit")
#pragma comment(linker, "/export:unigo_engine_shutdown")
#pragma comment(linker, "/export:unigo_engine_query_render_support")
#pragma comment(linker, "/export:unigo_engine_get_vsync")
#pragma comment(linker, "/export:unigo_engine_set_vsync")
#pragma comment(linker, "/export:unigo_engine_get_msaa")
#pragma comment(linker, "/export:unigo_engine_get_setting_string")
#pragma comment(linker, "/export:unigo_engine_set_msaa")
#pragma comment(linker, "/export:unigo_engine_set_window_size")
#pragma comment(linker, "/export:unigo_engine_set_window_mode")
#pragma comment(linker, "/export:unigo_engine_get_renderer")
#pragma comment(linker, "/export:unigo_engine_is_key_pressed")
#pragma comment(linker, "/export:unigo_render_setup")
#pragma comment(linker, "/export:unigo_render_apply")
#pragma comment(linker, "/export:unigo_last_error")
#endif

/* ---- create:创建并启动 Godot 内核 ---- */
UNIGO_API unigo_handle unigo_engine_create(const unigo_config *p_cfg) {
	unigo_set_error(nullptr);

	if (p_cfg == nullptr) {
		unigo_set_error("unigo_engine_create: 缺少 config");
		return nullptr;
	}

	const char *project_path = p_cfg->project_path;
	const char *execpath = p_cfg->execpath;
	const char **argv = p_cfg->argv;
	int argc = p_cfg->argc;

	/* 参数校验:execpath 是 Main::setup 的硬性要求,缺失直接失败。 */
	if (execpath == nullptr) {
		unigo_set_error("unigo_engine_create: 缺少 execpath");
		return nullptr;
	}

	/* 构造 argv:优先用宿主提供的 argv[0],否则以 execpath 作为 argv[0]。 */
	/* project_path 非空时注入 --path;其余宿主参数保持原序(如 --unigo-render-only)。 */
	/* 上限 128:配置体系需传多条 --unigo-config key=value(此前 16 会静默截断尾部配置,违反绝对控制)。 */
	const char *argv_buf[128];
	const bool has_host_argv = argc > 0 && argv != nullptr;
	const int host_argc = has_host_argv ? argc : 0;
	int argn = 1;
	argv_buf[0] = has_host_argv && argv[0] != nullptr ? argv[0] : execpath;
	if (project_path != nullptr && project_path[0] != '\0') {
		argv_buf[argn++] = "--path";
		argv_buf[argn++] = project_path;
	}
	/* 外部窗直渲:native_window 非 0 时注入 --unigo-external-window <HWND>,由 main.cpp
	 * 解析传给 DisplayServer::create 的 p_parent_window——Godot 不自主建窗,直接渲染进
	 * C# 宿主已建的窗口(HWND 以十进制传入,64 位安全)。 */
	char hwnd_arg[32];
	if (p_cfg->native_window != 0) {
		if (argn + 2 >= 128) {
			unigo_set_error("unigo_engine_create: 宿主参数超过 argv 上限 128");
			return nullptr;
		}
		argv_buf[argn++] = "--unigo-external-window";
		/* uintptr_t 转十进制(64 位 HWND 可能超出 int32,不用 to_int)。 */
		snprintf(hwnd_arg, sizeof(hwnd_arg), "%llu", (unsigned long long)p_cfg->native_window);
		argv_buf[argn++] = hwnd_arg;
	}
	for (int i = has_host_argv ? 1 : 0; i < host_argc; i++) {
		if (argn >= 128) {
			/* argv 超上限:显式失败而非静默截断(静默丢配置与宿主声明不一致)。 */
			unigo_set_error("unigo_engine_create: 宿主参数超过 argv 上限 128");
			return nullptr;
		}
		argv_buf[argn++] = argv[i];
	}
	char *argv_ptrs[128];
	for (int i = 0; i < argn; i++) {
		argv_ptrs[i] = const_cast<char *>(argv_buf[i]);
	}

	/* libgodot create:内部 Main::setup + initialize(含 OS_Windows 模块句柄处理)。 */
	/* 返回 GodotInstance(经 GDExtensionObjectPtr 伪装);失败返回 nullptr。 */
	/* init_func 必须非空(libgodot 强制要求),提供最小 GDExtension init。 */
	GodotInstance *instance = (GodotInstance *)libgodot_create_godot_instance(argn, argv_ptrs, unigo_minimal_extension_init);
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

/* 渲染状态:存根 RID(宿主分配的 uint64 id → Godot RID)。 */
struct UnigoRenderState {
	RID scenario;           /* 根窗口 World3D scenario */
	RID viewport;           /* 根窗口 viewport */
	RID camera;             /* C# 建的相机 */
	RID directional_light;  /* C# 建的平行光 */
	RID light_instance;     /* 平行光实例(挂 scenario) */
	bool setup_done = false; /* setup 是否已执行(幂等保护) */
	std::map<uint64_t, RID> handles; /* 真实 id → Godot RID(真实 id 由 C++ 分配) */
	std::set<uint64_t> instance_ids; /* 记录 instance 真实 id(供变换/可见性命令校验类型) */
	std::map<uint64_t, uint64_t> request_to_real; /* 宿主 request_id → 真实 id(宿主后续用真实 id 引用) */
	std::map<uint64_t, uint64_t> material_shader; /* material 真实 id → 关联 shader 真实 id(销毁时级联释放) */
	std::map<uint64_t, uint64_t> instance_light_base; /* instance 真实 id → 关联 light 基础 RID 真实 id(方向光:销毁时级联释放 light,防 RID 泄漏) */
	uint64_t next_handle = 1;        /* 真实 id 分配器(C++ 权威分配) */
};

/* 前向声明:shutdown 释放渲染资源时调用(定义见 render 段)。 */
static UnigoRenderState *unigo_render_get_state(UnigoEngineState *p_state);

/* 分配真实 id:从 1 递增,最高位恒为 0(与宿主 request_id 高位标志区间严格隔离)。
 * 理论不可能耗尽(2^63 个),溢出防御:若到高位区间则报错(防遮蔽)。 */
static uint64_t unigo_alloc_real_id(UnigoRenderState *render) {
	if (render->next_handle >= (1ULL << 62)) {
		return 0; /* 耗尽:调用方视为分配失败 */
	}
	return render->next_handle++;
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
		/* 释放所有渲染资源 RID(产品级:退出时清理,防泄漏)。 */
		UnigoRenderState *render = unigo_render_get_state(state);
		RenderingServer *rs = RenderingServer::get_singleton();
		if (rs != nullptr && render != nullptr) {
			/* 先释放 setup 创建的场景级资源(camera/light/light_instance)。 */
			if (render->setup_done) {
				rs->free_rid(render->light_instance);
				rs->free_rid(render->directional_light);
				rs->free_rid(render->camera);
				render->setup_done = false;
			}
			for (auto &pair : render->handles) {
				rs->free_rid(pair.second);
			}
			render->handles.clear();
			render->instance_ids.clear();
			render->request_to_real.clear();
			render->material_shader.clear();
			render->instance_light_base.clear();
		}
		state->instance->stop();
		libgodot_destroy_godot_instance((GDExtensionObjectPtr)state->instance);
		state->instance = nullptr;
	}

	/* C ABI 没有独立的句柄释放入口,保留轻量状态作为 shutdown 后的安全墓碑。 */
}

/* ---- query_render_support:窗口渲染支持探测 ---- */
UNIGO_API int32_t unigo_engine_query_render_support(void) {
	unigo_set_error(nullptr);

	/* 第一阶段:Windows 桌面默认有图形环境,固定返回支持。 */
	/* 后续接入 DisplayServer 真实探测(无图形会话等场景)。 */
	return 1;
}

/* ---- 运行时配置接口(统一模式:收 unigo_handle + 锁内校验生命周期) ---- */
/* 全部须在引擎线程调用(与 unigo_engine_iterate 同线程)——操作 SceneTree/
 * DisplayServer 等主线程单例;宿主主循环单线程满足,跨线程需自行同步。 */

/* ---- get_vsync:查询主窗口当前 vsync 模式(0=DISABLED,1=ENABLED,2=ADAPTIVE,3=MAILBOX) ---- */
UNIGO_API int32_t unigo_engine_get_vsync(unigo_handle p_handle) {
	unigo_set_error(nullptr);
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds == nullptr || ds->get_name() == "headless") {
		result = -1; /* 无窗口后端(空或 headless),宿主按不可用处理——headless 的 window_get_vsync_mode 固定返回 ENABLED,会伪造验证 */
	} else {
		DisplayServerEnums::VSyncMode mode = ds->window_get_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID);
		result = (int32_t)mode;
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- set_vsync:设置主窗口 vsync 模式(引擎线程;p_mode 0-3) ---- */
UNIGO_API int32_t unigo_engine_set_vsync(unigo_handle p_handle, int32_t p_mode) {
	unigo_set_error(nullptr);
	if (p_mode < (int32_t)DisplayServerEnums::VSYNC_DISABLED || p_mode > (int32_t)DisplayServerEnums::VSYNC_MAILBOX) {
		unigo_set_error("unigo_engine_set_vsync: 非法 vsync 模式");
		return -UNIGO_ERR_INVALID_ARG;
	}
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result = 0;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds == nullptr || ds->get_name() == "headless") {
		unigo_set_error("unigo_engine_set_vsync: 无窗口后端(空或 headless)");
		result = -UNIGO_ERR_UNSUPPORTED; /* headless 的 window_set_vsync_mode 是空操作——不能假成功 */
	} else {
		ds->window_set_vsync_mode((DisplayServerEnums::VSyncMode)p_mode, DisplayServerEnums::MAIN_WINDOW_ID);
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- get_msaa:查询根 viewport 当前 3D MSAA 档位(Viewport::MSAA 索引 0-3) ---- */
UNIGO_API int32_t unigo_engine_get_msaa(unigo_handle p_handle) {
	unigo_set_error(nullptr);
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result;
	SceneTree *st = SceneTree::get_singleton();
	DisplayServer *ds = DisplayServer::get_singleton();
	if (st == nullptr || st->get_root() == nullptr || ds == nullptr || ds->get_name() == "headless") {
		/* SceneTree 未就绪或无窗口后端(headless 的 get_msaa_3d 只读 Viewport 缓存属性,
		 * RasterizerDummy 无实际 MSAA——返回不可用防伪造验证) */
		result = -1;
	} else {
		result = (int32_t)st->get_root()->get_msaa_3d();
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- get_setting_string:回读任意 ProjectSettings 配置(文本化) ---- */
/* 返回 0=成功;1=key 不存在;-2=缓冲不足;负值=参数非法/内核 shutdown。 */
UNIGO_API int32_t unigo_engine_get_setting_string(unigo_handle p_handle, const char *p_key, char *p_buf, int32_t p_buf_size) {
	unigo_set_error(nullptr);
	if (p_key == nullptr || p_buf == nullptr || p_buf_size <= 0) {
		return -1;
	}
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result = 0;
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr || !ps->has_setting(p_key)) {
		result = 1; /* key 不存在 */
	} else if (!ps->is_builtin_setting(p_key)) {
		/* key 存在但非 Godot 注册(builtin)——是宿主 set_setting 自创(拼错/Godot 改名后残留)。
		 * 返回 -3:宿主配置验证据此发现 key 漂移(自证回读无法区分,review 关键 finding)。 */
		unigo_set_error("unigo_engine_get_setting_string: key 非 Godot 注册(疑似拼错/已改名)");
		result = -3;
	} else {
		Variant v = ps->get_setting(p_key);
		String s = v.stringify();
		errno_t e = strncpy_s(p_buf, p_buf_size, s.utf8().get_data(), _TRUNCATE);
		if (e == STRUNCATE) {
			unigo_set_error("unigo_engine_get_setting_string: 缓冲不足");
			result = -2; /* 缓冲不足:宿主应扩大重试 */
		}
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- set_msaa:运行时改根 viewport 3D MSAA(引擎线程;p_msaa 索引 0-3) ---- */
UNIGO_API int32_t unigo_engine_set_msaa(unigo_handle p_handle, int32_t p_msaa) {
	unigo_set_error(nullptr);
	if (p_msaa < (int32_t)Viewport::MSAA_DISABLED || p_msaa >= (int32_t)Viewport::MSAA_MAX) {
		unigo_set_error("unigo_engine_set_msaa: 非法 MSAA 档位");
		return -UNIGO_ERR_INVALID_ARG;
	}
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result = 0;
	SceneTree *st = SceneTree::get_singleton();
	DisplayServer *ds = DisplayServer::get_singleton();
	if (st == nullptr || st->get_root() == nullptr || ds == nullptr || ds->get_name() == "headless") {
		/* SceneTree 未就绪或无窗口后端(headless:set_msaa_3d 只改 Viewport 缓存属性,
		 * RasterizerDummy 无实际 MSAA——返回 UNSUPPORTED 防假成功) */
		unigo_set_error("unigo_engine_set_msaa: SceneTree 未就绪或无窗口后端(headless)");
		result = -UNIGO_ERR_UNSUPPORTED;
	} else {
		/* 注:gl_compatibility(GLES3)也实现了 3D MSAA(render_scene_buffers_gles3.cpp
		 * 按设备能力建 multisample 缓冲)——不按渲染方法名拒绝,仅 headless 无实际渲染 */
		st->get_root()->set_msaa_3d((Viewport::MSAA)p_msaa); /* 重建 render buffers;须引擎线程(ERR_MAIN_THREAD_GUARD) */
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- set_window_size:运行时改主窗口尺寸(像素)(引擎线程) ---- */
UNIGO_API int32_t unigo_engine_set_window_size(unigo_handle p_handle, int32_t p_width, int32_t p_height) {
	unigo_set_error(nullptr);
	if (p_width <= 0 || p_height <= 0) {
		unigo_set_error("unigo_engine_set_window_size: 非法尺寸");
		return -UNIGO_ERR_INVALID_ARG;
	}
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result = 0;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds == nullptr || ds->get_name() == "headless") {
		unigo_set_error("unigo_engine_set_window_size: 无窗口后端(headless)");
		result = -UNIGO_ERR_UNSUPPORTED;
	} else {
		ds->window_set_size(Size2i(p_width, p_height), DisplayServerEnums::MAIN_WINDOW_ID);
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- set_window_mode:运行时改主窗口模式(引擎线程;p_mode 0-4) ---- */
UNIGO_API int32_t unigo_engine_set_window_mode(unigo_handle p_handle, int32_t p_mode) {
	unigo_set_error(nullptr);
	if (p_mode < (int32_t)DisplayServerEnums::WINDOW_MODE_WINDOWED || p_mode > (int32_t)DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN) {
		unigo_set_error("unigo_engine_set_window_mode: 非法窗口模式");
		return -UNIGO_ERR_INVALID_ARG;
	}
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result = 0;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds == nullptr || ds->get_name() == "headless") {
		unigo_set_error("unigo_engine_set_window_mode: 无窗口后端(headless)");
		result = -UNIGO_ERR_UNSUPPORTED;
	} else {
		ds->window_set_mode((DisplayServerEnums::WindowMode)p_mode, DisplayServerEnums::MAIN_WINDOW_ID);
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- get_renderer:回读当前渲染方法(forward_plus/mobile/gl_compatibility) ---- */
/* 供宿主确认渲染器真实生效。返回 0=成功;-2=缓冲不足;负值=内核未初始化/shutdown。 */
UNIGO_API int32_t unigo_engine_get_renderer(unigo_handle p_handle, char *p_buf, int32_t p_buf_size) {
	unigo_set_error(nullptr);
	if (p_buf == nullptr || p_buf_size <= 0) {
		return -1;
	}
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return -UNIGO_ERR_SHUTDOWN;
	}
	int32_t result = 0;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (OS::get_singleton() == nullptr) {
		unigo_set_error("unigo_engine_get_renderer: OS 未初始化");
		result = -UNIGO_ERR_UNSUPPORTED;
	} else if (ds == nullptr || ds->get_name() == "headless") {
		/* headless 用 RasterizerDummy,current_rendering_method 只是配置缓存值非真实
		 * 渲染后端——返回 UNSUPPORTED 防宿主"真实渲染器生效"校验误通过 */
		unigo_set_error("unigo_engine_get_renderer: 无窗口渲染后端(headless)");
		result = -UNIGO_ERR_UNSUPPORTED;
	} else {
		String m = OS::get_singleton()->get_current_rendering_method();
		errno_t e = strncpy_s(p_buf, p_buf_size, m.utf8().get_data(), _TRUNCATE);
		if (e == STRUNCATE) {
			unigo_set_error("unigo_engine_get_renderer: 缓冲不足");
			result = -2;
		}
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- is_key_pressed:查询 Godot Input 单例当前帧按键状态(引擎线程) ---- */
/* p_keycode:Godot Key 枚举原始 int 值(含 SPECIAL 高位,如 F11=SPECIAL|0x26)。
 * 返回 1=按下 0=未按;内核未就绪/无 Input 单例返回 0。供宿主(如验证宿主
 * 按键切窗口模式)经统一 C ABI 读输入——Engine 完整输入系统是 M7 范围,此处只读查询。 */
UNIGO_API int32_t unigo_engine_is_key_pressed(unigo_handle p_handle, int32_t p_keycode) {
	unigo_set_error(nullptr);
	UnigoEngineState *state = unigo_lock_state(p_handle);
	if (state == nullptr) {
		return 0;
	}
	int32_t result = 0;
	if (Input::get_singleton() != nullptr) {
		result = Input::get_singleton()->is_key_pressed((Key)p_keycode) ? 1 : 0;
	}
	state->instance_mutex.unlock();
	return result;
}

/* ---- 渲染命令缓冲(C# 驱动 RenderingServer,不经场景树) ---- */
/*
 * 验证目标:"C# 直接驱动 Godot RenderingServer 渲染,而非 Godot 场景树/节点"。
 * 实现要点:
 *  - 根窗口 viewport 由 SceneTree 建立(引擎循环必须 SceneTree,绕开的是节点内容);
 *  - 我们拿根窗口 viewport 的 World3D scenario,把 C# 建的 mesh/instance/light/camera 挂进去;
 *  - 材质用 shader_create_from_code 手写最小 unlit shader(绕开 StandardMaterial 场景层);
 *  - 命令缓冲:宿主每帧批量填 POD 命令,一次 unigo_render_apply 消费(架构 §8 批量热路径)。
 */

/* 解析引用 → RID:先查本批 request_id 映射,再查真实 id handles。
 * (同批命令内用 request_id 引用(真实 id 尚未回传);跨批用真实 id——
 *  request_to_real 每批结束清空,不会与真实 id 数值空间冲突。) */
static bool unigo_find_rid(UnigoRenderState *render, uint64_t ref, RID *r_out) {
	auto req = render->request_to_real.find(ref);
	if (req != render->request_to_real.end()) {
		auto hit = render->handles.find(req->second);
		if (hit != render->handles.end()) {
			*r_out = hit->second;
			return true;
		}
	}
	auto it = render->handles.find(ref);
	if (it != render->handles.end()) {
		*r_out = it->second;
		return true;
	}
	return false;
}


/* 取渲染状态(挂在 UnigoEngineState 后)。 */
static UnigoRenderState *unigo_render_get_state(UnigoEngineState *p_state) {
	/* 复用 instance 指针尾部内存?不:独立分配,由宿主生命周期管理。 */
	/* 简化为全局单例(单实例宿主,一个内核一个渲染状态)。 */
	static UnigoRenderState s_render;
	return &s_render;
}

/* ---- render_setup:创建 scenario/camera/light,挂到根窗口 viewport ---- */
UNIGO_API int32_t unigo_render_setup(unigo_handle p_handle) {
	unigo_set_error(nullptr);
	if (p_handle == nullptr) {
		unigo_set_error("unigo_render_setup: 句柄为空");
		return -UNIGO_ERR_INVALID_ARG;
	}
	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	if (state->shutdown_done.load() || state->instance == nullptr) {
		unigo_set_error("unigo_render_setup: 内核已关闭");
		return -UNIGO_ERR_SHUTDOWN;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		unigo_set_error("unigo_render_setup: RenderingServer 未初始化");
		return -UNIGO_ERR_INTERNAL;
	}

	/* 根窗口 viewport 与 World3D scenario(引擎已建好)。 */
	Window *root = SceneTree::get_singleton()->get_root();
	if (root == nullptr) {
		unigo_set_error("unigo_render_setup: 根窗口未创建");
		return -UNIGO_ERR_INTERNAL;
	}
	Viewport *root_viewport = root->get_viewport();
	Ref<World3D> world = root_viewport->get_world_3d();
	if (world.is_null()) {
		unigo_set_error("unigo_render_setup: 根 viewport 无 World3D");
		return -UNIGO_ERR_INTERNAL;
	}

	UnigoRenderState *render = unigo_render_get_state(state);

	/* 幂等保护:重复 setup 先释放旧资源(避免旧 RID 被覆盖遗失)。 */
	if (render->setup_done) {
		rs->free_rid(render->light_instance);
		rs->free_rid(render->directional_light);
		rs->free_rid(render->camera);
	}

	render->scenario = world->get_scenario();
	render->viewport = root_viewport->get_viewport_rid();

	/* 相机:透视,60° FOV,位置 (0,1.5,3) 看向原点(立方体中心)。 */
	render->camera = rs->camera_create();
	rs->camera_set_perspective(render->camera, 60.0f, 0.05f, 100.0f);
	rs->camera_set_transform(render->camera, Transform3D(Basis(), Vector3(0.0f, 1.5f, 3.0f)));
	rs->viewport_attach_camera(render->viewport, render->camera);

	render->setup_done = true;

	return UNIGO_OK;
}

/* ---- render_apply:消费一批命令 ---- */
/*
 * 命令缓冲方案 spike:命令是 POD,顶点数据不塞进命令(几何由 C++ 侧内置)。
 * UNIGO_RENDER_CREATE_CUBE_MESH:创建内置立方体网格(8 顶点 12 三角,带法线),
 * 并挂上纯色 unlit 材质(C# 只发 handle,不传几何数据)。
 * 这验证的是"C# 驱动 RenderingServer 渲染"链路本身,几何内置是为最小化命令缓冲。
 */
UNIGO_API int32_t unigo_render_apply(unigo_handle p_handle, const unigo_render_command *p_cmds, int32_t p_count, unigo_handle_result *p_results) {
	unigo_set_error(nullptr);
	if (p_handle == nullptr || p_cmds == nullptr || p_count < 0) {
		unigo_set_error("unigo_render_apply: 参数非法");
		return -UNIGO_ERR_INVALID_ARG;
	}
	UnigoEngineState *state = (UnigoEngineState *)p_handle;
	if (state->shutdown_done.load() || state->instance == nullptr) {
		unigo_set_error("unigo_render_apply: 内核已关闭");
		return -UNIGO_ERR_SHUTDOWN;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	UnigoRenderState *render = unigo_render_get_state(state);

	/* 每批独立命名空间:清空上次批次的 request_id 映射(本批内用 request_id 引用,
	 * 跨批用真实 id;防止失败残留/历史累积与真实 id 数值空间冲突)。 */
	render->request_to_real.clear();

	for (int32_t i = 0; i < p_count; i++) {
		const unigo_render_command &cmd = p_cmds[i];
		if (p_results != nullptr) {
			p_results[i].request_id = cmd.request_id;
			p_results[i].handle = 0; /* 默认无创建;创建命令会填真实 id */
		}
		switch (cmd.type) {
			case UNIGO_RENDER_CREATE_CUBE_MESH: {
				/* 立方体几何:24 顶点(每面 4 个,面法线)+ 36 索引。 */
				static const float s_verts[24][3] = {
					/* +X */ { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f},
					/* -X */ {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f},
					/* +Y */ {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
					/* -Y */ {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f},
					/* +Z */ {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
					/* -Z */ { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
				};
				static const float s_norms[24][3] = {
					/* +X */ { 1,0,0}, { 1,0,0}, { 1,0,0}, { 1,0,0},
					/* -X */ {-1,0,0}, {-1,0,0}, {-1,0,0}, {-1,0,0},
					/* +Y */ { 0,1,0}, { 0,1,0}, { 0,1,0}, { 0,1,0},
					/* -Y */ { 0,-1,0}, { 0,-1,0}, { 0,-1,0}, { 0,-1,0},
					/* +Z */ { 0,0,1}, { 0,0,1}, { 0,0,1}, { 0,0,1},
					/* -Z */ { 0,0,-1}, { 0,0,-1}, { 0,0,-1}, { 0,0,-1},
				};
				static const int32_t s_idx[36] = {
					/* +X */ 0,2,1, 0,3,2,
					/* -X */ 4,6,5, 4,7,6,
					/* +Y */ 8,9,10, 8,10,11,
					/* -Y */ 12,13,14, 12,14,15,
					/* +Z */ 16,18,17, 16,19,18,
					/* -Z */ 20,22,21, 20,23,22,
				};

				PackedVector3Array verts;
				PackedVector3Array norms;
				verts.resize(24);
				norms.resize(24);
				for (int v = 0; v < 24; v++) {
					verts.set(v, Vector3(s_verts[v][0], s_verts[v][1], s_verts[v][2]));
					norms.set(v, Vector3(s_norms[v][0], s_norms[v][1], s_norms[v][2]));
				}
				PackedInt32Array idx;
				idx.resize(36);
				for (int k = 0; k < 36; k++) {
					idx.set(k, s_idx[k]);
				}

				Array arrays;
				arrays.resize(RSE::ARRAY_MAX);
				arrays[RSE::ARRAY_VERTEX] = verts;
				arrays[RSE::ARRAY_NORMAL] = norms;
				arrays[RSE::ARRAY_INDEX] = idx;

				RID mesh = rs->mesh_create();
				rs->mesh_add_surface_from_arrays(mesh, RSE::PRIMITIVE_TRIANGLES, arrays);
				uint64_t real_id = unigo_alloc_real_id(render);
				if (real_id == 0) { unigo_set_error("unigo_render_apply: 句柄空间耗尽"); return -UNIGO_ERR_INTERNAL; } /* C++ 权威分配真实 id */
				render->handles[real_id] = mesh;
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_CREATE_MATERIAL: {
				/* 最小受光 shader:ALBEDO 取命令颜色(C# 显式传色);PBR 光照——去掉 unshaded,材质吃场景灯光。 */
				float r = cmd.color[0], g = cmd.color[1], b = cmd.color[2];
				char albedo[128];
				snprintf(albedo, sizeof(albedo), "vec3(%.3f, %.3f, %.3f)", r, g, b);
				String shader_code = String("shader_type spatial;\n"
					"void fragment() { ALBEDO = ") + albedo + String("; ROUGHNESS = 0.8; METALLIC = 0.0; }\n");
				RID shader = rs->shader_create_from_code(shader_code);
				RID material = rs->material_create();
				rs->material_set_shader(material, shader);
				uint64_t real_id = unigo_alloc_real_id(render);
				if (real_id == 0) { unigo_set_error("unigo_render_apply: 句柄空间耗尽"); return -UNIGO_ERR_INTERNAL; } /* C++ 权威分配真实 id */
				render->handles[real_id] = material;
				/* shader 也是 RID,需一并登记以便 shutdown 释放(材质引用但不拥有 shader)。 */
				uint64_t shader_id = unigo_alloc_real_id(render);
				if (shader_id == 0) { unigo_set_error("unigo_render_apply: 句柄空间耗尽"); return -UNIGO_ERR_INTERNAL; }
				render->handles[shader_id] = shader;
				render->material_shader[real_id] = shader_id; /* 记录 material→shader 所有权(销毁时级联释放) */
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_SET_SURFACE_MATERIAL: {
				/* parent/value 支持真实 id 或 request_id(同批用 request_id,跨批用真实 id)。 */
				RID mesh_rid, mat_rid;
				if (!unigo_find_rid(render, cmd.parent, &mesh_rid) || !unigo_find_rid(render, cmd.value, &mat_rid)) {
					unigo_set_error("unigo_render_apply: 未知 mesh/material handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				rs->mesh_surface_set_material(mesh_rid, 0, mat_rid);
				break;
			}
			case UNIGO_RENDER_CREATE_INSTANCE: {
				/* parent = mesh 引用(真实 id 或 request_id)。 */
				RID mesh_rid;
				if (!unigo_find_rid(render, cmd.parent, &mesh_rid)) {
					unigo_set_error("unigo_render_apply: 未知 mesh handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				RID instance = rs->instance_create2(mesh_rid, render->scenario);
				uint64_t real_id = unigo_alloc_real_id(render);
				if (real_id == 0) { unigo_set_error("unigo_render_apply: 句柄空间耗尽"); return -UNIGO_ERR_INTERNAL; } /* C++ 权威分配真实 id */
				render->handles[real_id] = instance;
				render->instance_ids.insert(real_id);
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_SET_INSTANCE_TRANSFORM: {
				/* 类型校验:必须是 instance(防把 mesh/material id 误当 instance 传)。 */
				if (render->instance_ids.find(cmd.handle) == render->instance_ids.end()) {
					unigo_set_error("unigo_render_apply: handle 不是 instance");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				auto it = render->handles.find(cmd.handle);
				if (it == render->handles.end()) {
					unigo_set_error("unigo_render_apply: 未知 instance handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				/* POD 变换 → Godot Transform3D(basis 行主序)。 */
				Basis basis;
				basis.rows[0] = Vector3(cmd.transform.basis[0], cmd.transform.basis[1], cmd.transform.basis[2]);
				basis.rows[1] = Vector3(cmd.transform.basis[3], cmd.transform.basis[4], cmd.transform.basis[5]);
				basis.rows[2] = Vector3(cmd.transform.basis[6], cmd.transform.basis[7], cmd.transform.basis[8]);
				Transform3D xform(basis, Vector3(cmd.transform.origin[0], cmd.transform.origin[1], cmd.transform.origin[2]));
				rs->instance_set_transform(it->second, xform);
				break;
			}
			case UNIGO_RENDER_SET_INSTANCE_VISIBLE: {
				/* 类型校验:必须是 instance。 */
				if (render->instance_ids.find(cmd.handle) == render->instance_ids.end()) {
					unigo_set_error("unigo_render_apply: handle 不是 instance");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				auto it = render->handles.find(cmd.handle);
				if (it == render->handles.end()) {
					unigo_set_error("unigo_render_apply: 未知 instance handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				rs->instance_set_visible(it->second, cmd.value != 0);
				break;
			}
			case UNIGO_RENDER_SET_CAMERA: {
				/* 设相机:setup 已建 camera RID,这里只改透视参数与变换(C# 控制相机)。 */
				if (render->camera.is_null()) {
					unigo_set_error("unigo_render_apply: 相机未初始化(setup 未调用?)");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				float fov = (cmd.fparams[0] > 0.0f) ? cmd.fparams[0] : 60.0f;
				float near_plane = (cmd.fparams[1] > 0.0f) ? cmd.fparams[1] : 0.05f;
				float far_plane = (cmd.fparams[2] > 0.0f) ? cmd.fparams[2] : 100.0f;
				rs->camera_set_perspective(render->camera, fov, near_plane, far_plane);
				/* POD 变换 → Godot Transform3D(列向量约定:cmd.transform.basis 已按列映射)。 */
				Basis basis;
				basis.rows[0] = Vector3(cmd.transform.basis[0], cmd.transform.basis[1], cmd.transform.basis[2]);
				basis.rows[1] = Vector3(cmd.transform.basis[3], cmd.transform.basis[4], cmd.transform.basis[5]);
				basis.rows[2] = Vector3(cmd.transform.basis[6], cmd.transform.basis[7], cmd.transform.basis[8]);
				Transform3D xform(basis, Vector3(cmd.transform.origin[0], cmd.transform.origin[1], cmd.transform.origin[2]));
				rs->camera_set_transform(render->camera, xform);
				break;
			}
			case UNIGO_RENDER_CREATE_DIRECTIONAL_LIGHT: {
				/* 先校验参数(创建任何 RID 前)——非法能量不得泄漏已创建的 light RID。 */
				if (cmd.fparams[0] < 0.0f || !std::isfinite(cmd.fparams[0])) {
					unigo_set_error("unigo_render_apply: 灯光能量必须为非负有限值");
					return -UNIGO_ERR_INVALID_ARG;
				}
				/* 建方向光:登记进 handles(真实 id 权威分配),供 DESTROY 释放。 */
				RID light = rs->directional_light_create();
				rs->light_set_color(light, Color(cmd.color[0], cmd.color[1], cmd.color[2]));
				rs->light_set_param(light, RSE::LIGHT_PARAM_ENERGY, cmd.fparams[0]); /* 原值接受:0=关灯合法 */
				RID light_instance = rs->instance_create2(light, render->scenario);
				/* 方向 = 实体旋转方向:把 transform.basis 的 -Z 轴(光默认朝向)旋转后作为光照方向。 */
				Basis basis;
				basis.rows[0] = Vector3(cmd.transform.basis[0], cmd.transform.basis[1], cmd.transform.basis[2]);
				basis.rows[1] = Vector3(cmd.transform.basis[3], cmd.transform.basis[4], cmd.transform.basis[5]);
				basis.rows[2] = Vector3(cmd.transform.basis[6], cmd.transform.basis[7], cmd.transform.basis[8]);
				Transform3D xform(basis, Vector3(cmd.transform.origin[0], cmd.transform.origin[1], cmd.transform.origin[2]));
				rs->instance_set_transform(light_instance, xform);
				uint64_t real_id = unigo_alloc_real_id(render);
				if (real_id == 0) { unigo_set_error("unigo_render_apply: 句柄空间耗尽"); return -UNIGO_ERR_INTERNAL; }
				/* light 基础 RID 也要登记进 handles 并分配真实 id:shutdown 才能释放;DESTROY 经映射级联。 */
				uint64_t light_real_id = unigo_alloc_real_id(render);
				if (light_real_id == 0) { unigo_set_error("unigo_render_apply: 句柄空间耗尽"); return -UNIGO_ERR_INTERNAL; }
				render->handles[light_real_id] = light;
				render->instance_light_base[real_id] = light_real_id; /* instance→light 基础(id 供级联释放) */
				render->handles[real_id] = light_instance;
				render->instance_ids.insert(real_id); /* 灯光实例也是 instance(供 SetInstanceTransform 改方向) */
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_DESTROY: {
				/* 销毁对象并释放 RID(产品级:资源必须释放,防泄漏)。
				 * 幂等:未知 handle 静默成功——销毁不存在的资源无害,且使"重试整批销毁"安全
				 * (宿主批次可能部分成功,重试时已销毁项不再报错阻塞)。 */
				auto it = render->handles.find(cmd.handle);
				if (it == render->handles.end()) {
					break; /* 已销毁或从未创建:幂等跳过。 */
				}
				/* 若销毁的是材质,级联释放其关联 shader(材质引用但不拥有 shader)。 */
				auto shader_it = render->material_shader.find(cmd.handle);
				if (shader_it != render->material_shader.end()) {
					auto shader_handle = render->handles.find(shader_it->second);
					if (shader_handle != render->handles.end()) {
						rs->free_rid(shader_handle->second);
						render->handles.erase(shader_handle);
					}
					render->material_shader.erase(shader_it);
				}
				/* 若销毁的是方向光 instance,级联释放其 light 基础 RID(防基础 light RID 泄漏)。 */
				auto light_it = render->instance_light_base.find(cmd.handle);
				if (light_it != render->instance_light_base.end()) {
					auto light_handle = render->handles.find(light_it->second);
					if (light_handle != render->handles.end()) {
						rs->free_rid(light_handle->second);
						render->handles.erase(light_handle);
					}
					render->instance_light_base.erase(light_it);
				}
				rs->free_rid(it->second);
				render->handles.erase(it);
				render->instance_ids.erase(cmd.handle); /* 若销毁的是 instance,移出类型集合 */
				/* 同步清理 request_to_real 中指向该真实 id 的条目(不报错,尽力而为)。 */
				for (auto rit = render->request_to_real.begin(); rit != render->request_to_real.end();) {
					if (rit->second == cmd.handle) {
						rit = render->request_to_real.erase(rit);
					} else {
						++rit;
					}
				}
				break;
			}
			default:
				unigo_set_error("unigo_render_apply: 未知命令类型");
				return -UNIGO_ERR_INVALID_ARG;
		}
	}
	/* 本批命令结束:清空本批 request_id 映射(避免历史累积与真实 id 数值空间冲突)。
	 * 跨批引用一律用真实 id(handles),不经 request_to_real。 */
	render->request_to_real.clear();
	return UNIGO_OK;
}

/* ---- last_error:取线程局部诊断 ---- */
UNIGO_API const char *unigo_last_error(void) {
	return s_has_error ? s_last_error : nullptr;
}

} // extern "C"
