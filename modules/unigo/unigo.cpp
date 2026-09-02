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
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include <atomic>
#include <map>
#include <mutex>
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
	/* project_path 非空时注入 --path;其余宿主参数保持原序(如 --unigo-render-only --quiet)。 */
	/* 上限 16 是第一阶段保护(冒烟场景参数极少),超出部分按尾部参数截断。 */
	const char *argv_buf[16];
	const bool has_host_argv = argc > 0 && argv != nullptr;
	const int host_argc = has_host_argv ? argc : 0;
	int argn = 1;
	argv_buf[0] = has_host_argv && argv[0] != nullptr ? argv[0] : execpath;
	if (project_path != nullptr && project_path[0] != '\0') {
		argv_buf[argn++] = "--path";
		argv_buf[argn++] = project_path;
	}
	for (int i = has_host_argv ? 1 : 0; i < host_argc && argn < 16; i++) {
		argv_buf[argn++] = argv[i];
	}
	char *argv_ptrs[16];
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

/* ---- query_render_support:窗口渲染支持探测 ---- */
UNIGO_API int32_t unigo_engine_query_render_support(void) {
	unigo_set_error(nullptr);

	/* 第一阶段:Windows 桌面默认有图形环境,固定返回支持。 */
	/* 后续接入 DisplayServer 真实探测(无图形会话等场景)。 */
	return 1;
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

/* 渲染状态:存根 RID(宿主分配的 uint64 id → Godot RID)。 */
struct UnigoRenderState {
	RID scenario;           /* 根窗口 World3D scenario */
	RID viewport;           /* 根窗口 viewport */
	RID camera;             /* C# 建的相机 */
	RID directional_light;  /* C# 建的平行光 */
	std::map<uint64_t, RID> handles; /* 真实 id → Godot RID(真实 id 由 C++ 分配) */
	std::map<uint64_t, uint64_t> request_to_real; /* 宿主 request_id → 真实 id(宿主后续用真实 id 引用) */
	uint64_t next_handle = 1;        /* 真实 id 分配器(C++ 权威分配) */
};

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
	render->scenario = world->get_scenario();
	render->viewport = root_viewport->get_viewport_rid();

	/* 相机:透视,60° FOV,位置 (0,1.5,3) 看向原点(立方体中心)。 */
	render->camera = rs->camera_create();
	rs->camera_set_perspective(render->camera, 60.0f, 0.05f, 100.0f);
	rs->camera_set_transform(render->camera, Transform3D(Basis(), Vector3(0.0f, 1.5f, 3.0f)));
	rs->viewport_attach_camera(render->viewport, render->camera);

	/* 平行光:默认方向(斜上方),白色。 */
	render->directional_light = rs->directional_light_create();
	rs->light_set_color(render->directional_light, Color(1.0f, 1.0f, 1.0f));
	rs->light_set_param(render->directional_light, RSE::LIGHT_PARAM_ENERGY, 1.0f);
	RID light_instance = rs->instance_create2(render->directional_light, render->scenario);
	rs->instance_set_transform(light_instance, Transform3D(Basis::from_euler(Vector3(-0.5f, 0.5f, 0.0f)), Vector3()));

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
					/* +X */ 0,1,2, 0,2,3,
					/* -X */ 4,5,6, 4,6,7,
					/* +Y */ 8,9,10, 8,10,11,
					/* -Y */ 12,13,14, 12,14,15,
					/* +Z */ 16,17,18, 16,18,19,
					/* -Z */ 20,21,22, 20,22,23,
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
				uint64_t real_id = render->next_handle++; /* C++ 权威分配真实 id */
				render->handles[real_id] = mesh;
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_CREATE_MATERIAL: {
				/* 最小 unlit shader:ALBEDO 取命令颜色(绕开 StandardMaterial 场景层)。 */
				float r = cmd.color[0], g = cmd.color[1], b = cmd.color[2];
				/* 默认白(未设颜色时)。 */
				if (r == 0 && g == 0 && b == 0) { r = 1; g = 1; b = 1; }
				char albedo[128];
				snprintf(albedo, sizeof(albedo), "vec3(%.3f, %.3f, %.3f)", r, g, b);
				String shader_code = String("shader_type spatial;\n"
					"render_mode unshaded;\n"
					"void fragment() { ALBEDO = ") + albedo + String("; }\n");
				RID shader = rs->shader_create_from_code(shader_code);
				RID material = rs->material_create();
				rs->material_set_shader(material, shader);
				uint64_t real_id = render->next_handle++; /* C++ 权威分配真实 id */
				render->handles[real_id] = material;
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_SET_SURFACE_MATERIAL: {
				/* parent/value = mesh/material 的宿主 request_id,解析为真实 id。 */
				auto mesh_req = render->request_to_real.find(cmd.parent);
				auto mat_req = render->request_to_real.find(cmd.value);
				if (mesh_req == render->request_to_real.end() || mat_req == render->request_to_real.end()) {
					unigo_set_error("unigo_render_apply: 未知 mesh/material request_id");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				auto mesh_it = render->handles.find(mesh_req->second);
				auto mat_it = render->handles.find(mat_req->second);
				if (mesh_it == render->handles.end() || mat_it == render->handles.end()) {
					unigo_set_error("unigo_render_apply: 未知 mesh/material handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				rs->mesh_surface_set_material(mesh_it->second, 0, mat_it->second);
				break;
			}
			case UNIGO_RENDER_CREATE_INSTANCE: {
				/* parent = mesh 的宿主 request_id,解析为真实 id。 */
				auto req_it = render->request_to_real.find(cmd.parent);
				if (req_it == render->request_to_real.end()) {
					unigo_set_error("unigo_render_apply: 未知 mesh request_id");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				uint64_t mesh_real = req_it->second;
				auto mesh_it = render->handles.find(mesh_real);
				if (mesh_it == render->handles.end()) {
					unigo_set_error("unigo_render_apply: 未知 mesh handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				RID instance = rs->instance_create2(mesh_it->second, render->scenario);
				uint64_t real_id = render->next_handle++; /* C++ 权威分配真实 id */
				render->handles[real_id] = instance;
				render->request_to_real[cmd.request_id] = real_id;
				if (p_results != nullptr) { p_results[i].handle = real_id; }
				break;
			}
			case UNIGO_RENDER_SET_INSTANCE_TRANSFORM: {
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
				auto it = render->handles.find(cmd.handle);
				if (it == render->handles.end()) {
					unigo_set_error("unigo_render_apply: 未知 instance handle");
					return -UNIGO_ERR_INVALID_HANDLE;
				}
				rs->instance_set_visible(it->second, cmd.value != 0);
				break;
			}
			default:
				unigo_set_error("unigo_render_apply: 未知命令类型");
				return -UNIGO_ERR_INVALID_ARG;
		}
	}
	return UNIGO_OK;
}

/* ---- last_error:取线程局部诊断 ---- */
UNIGO_API const char *unigo_last_error(void) {
	return s_has_error ? s_last_error : nullptr;
}

} // extern "C"
