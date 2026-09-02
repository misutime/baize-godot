/**************************************************************************/
/*  unigo.h                                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         UNIGO ENGINE (Fork)                            */
/*                    https://github.com/misutime/unigo-godot-kernel      */
/**************************************************************************/
/* UniGo 宿主与 Godot Kernel 之间的窄 C ABI(总体架构 §8 Native Bridge)。 */
/* 职责:稳定隔离 Godot 变化,导出入口少而稳定;宿主(C# Player.App)只经    */
/* 本层驱动内核的初始化 / tick / shutdown / 渲染,参数一律定宽整数、POD、 */
/* UTF-8 与不透明 Handle,每次调用返回明确错误码,不让异常穿越 ABI。       */
/*                                                                        */
/* 本头文件仅声明 C ABI;实现见 unigo.cpp(内部封装 GodotInstance + Main)。*/
/* 编译为 shared_library(template_release 纯净渲染内核)时导出。          */
/**************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* ---- 导出宏:shared_library 构建时 DLL 导出 ---- */
#if defined(_MSC_VER) || defined(__MINGW32__)
#define UNIGO_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define UNIGO_API __attribute__((visibility("default")))
#else
#define UNIGO_API
#endif

/* ---- 统一错误码(与 UniGo.Core 的 ErrorCode 保持同一语义序) ---- */
enum {
	UNIGO_OK = 0,             /* 成功 */
	UNIGO_ERR_INVALID_ARG = 1,/* 参数非法(空指针、非法句柄) */
	UNIGO_ERR_OUT_OF_MEMORY = 2, /* 内存不足 */
	UNIGO_ERR_INIT_FAILED = 3,   /* 内核初始化失败(create/setup/start 失败) */
	UNIGO_ERR_INVALID_HANDLE = 4, /* 句柄已失效/悬空 */
	UNIGO_ERR_UNSUPPORTED = 5,   /* 当前平台/后端不支持该操作 */
	UNIGO_ERR_INTERNAL = 6,      /* 内部错误(迭代异常等) */
	UNIGO_ERR_SHUTDOWN = 7,      /* 已进入 shutdown 状态 */
};

/* ---- 引擎句柄:不透明指针,宿主不得解引用,只作身份传递 ---- */
typedef void *unigo_handle;

/* ---- 引擎配置(POD,UTF-8 字符串) ---- */
typedef struct unigo_config {
	const char *project_path;   /* Godot 项目路径(可选,传 NULL 用默认;纯净模式不传) */
	const char *execpath;       /* 宿主可执行文件路径(必填,Main::setup 需要) */
	const char **argv;          /* 命令行参数(可选,如 --unigo-render-only --quiet) */
	int argc;                   /* argv 长度 */
} unigo_config;

/**
 * 创建 Godot 内核实例并启动。
 * 返回不透明句柄;失败返回 NULL,可用 unigo_last_error() 取诊断。
 * 内部序列:Main::setup(execpath) → Main::setup2() → Main::start()。
 */
UNIGO_API unigo_handle unigo_engine_create(const unigo_config *p_cfg);

/**
 * 驱动内核一帧(Main::iteration)。
 * @return 0=继续运行;1=引擎请求退出;负值=错误码(见 UNIGO_ERR_*)。
 */
UNIGO_API int32_t unigo_engine_iterate(unigo_handle p_handle);

/**
 * 请求内核退出(设置退出标志,由后续 iterate 返回 1 或直接停止)。
 * 幂等;可在任意线程调用;shutdown 后返回正值 UNIGO_ERR_SHUTDOWN。
 */
UNIGO_API int32_t unigo_engine_request_exit(unigo_handle p_handle);

/**
 * 销毁内核实例(内部 Main::cleanup)。幂等;句柄此后调用其他接口会安全返回错误。
 */
UNIGO_API void unigo_engine_shutdown(unigo_handle p_handle);

/**
 * 查询当前平台/后端是否支持窗口渲染(无图形环境返回 0)。
 * 供宿主决定是否创建窗口。
 */
UNIGO_API int32_t unigo_engine_query_render_support(void);

/* ---- vsync 查询/设置(宿主显式控制帧节奏边界) ---- */
/* 模式枚举与 DisplayServerEnums::VSyncMode 一致:0=DISABLED,1=ENABLED,2=ADAPTIVE,3=MAILBOX。
 * get:DisplayServer 未就绪/非窗口后端返回 -1;set:非法模式返回 -UNIGO_ERR_INVALID_ARG,
 * DisplayServer 未就绪返回 -UNIGO_ERR_UNSUPPORTED。 */
UNIGO_API int32_t unigo_engine_get_vsync(void);
UNIGO_API int32_t unigo_engine_set_vsync(int32_t p_mode);
UNIGO_API int32_t unigo_engine_get_msaa(void);

/* ---- 渲染命令缓冲(C# 驱动 RenderingServer,不经场景树) ---- */
/*
 * 目标:验证"C# 直接驱动 Godot RenderingServer 渲染,而非 Godot 场景树"。
 * 命令缓冲方案:宿主(C#)每帧批量填 POD 命令数组,一次 unigo_render_apply 消费。
 * 与总体架构 §8 一致(Transform 等热路径提供批量 API;参数用 POD/定宽整数)。
 */

/* 渲染命令类型枚举。 */
enum {
	UNIGO_RENDER_NONE = 0,
	UNIGO_RENDER_CREATE_CUBE_MESH,     /* 建内置立方体网格(payload: handle=mesh id) */
	UNIGO_RENDER_CREATE_MATERIAL,      /* 建材质(payload: handle=material id) */
	UNIGO_RENDER_SET_SURFACE_MATERIAL, /* 设表面材质(payload: parent=mesh id, value=material id) */
	UNIGO_RENDER_CREATE_INSTANCE,      /* 建实例(payload: handle=instance id, parent=mesh id) */
	UNIGO_RENDER_SET_INSTANCE_TRANSFORM, /* 设实例变换(payload: handle=instance id, transform) */
	UNIGO_RENDER_SET_INSTANCE_VISIBLE,   /* 设实例可见(payload: handle=instance id, value=bool) */
	UNIGO_RENDER_DESTROY,              /* 销毁对象(payload: handle=真实 id;释放 RID) */
	UNIGO_RENDER_SET_CAMERA,           /* 设相机(payload: transform=相机变换;fparams[0]=FOV度,[1]=near,[2]=far) */
	UNIGO_RENDER_CREATE_DIRECTIONAL_LIGHT, /* 建方向光(payload: color=光色;transform=方向) */
};

/* 变换 POD(行主序,与 Godot Transform3D 对齐)。 */
typedef struct unigo_transform {
	float basis[9];  /* 3x3 旋转/缩放 */
	float origin[3]; /* 平移 */
} unigo_transform;

/* 渲染命令 POD。type 决定 payload 如何解释。 */
typedef struct unigo_render_command {
	uint32_t type;
	uint64_t request_id;  /* 宿主请求标记(创建命令时,每对象唯一;宿主用它匹配回传的真实 id) */
	uint64_t handle;      /* 真实对象 id(创建命令=C++ 分配并回传;引用命令=引用已创建对象的真实 id) */
	uint64_t parent;      /* 关联对象真实 id(如 instance 关联 mesh) */
	uint64_t value;       /* 标量(如可见性 bool;SetSurfaceMaterial 的 material 真实 id) */
	float color[4];       /* 材质颜色 RGB(仅 CREATE_MATERIAL 用;宿主必须显式赋值,零值=黑色合法;A 当前未使用) */
	float fparams[4];     /* 通用浮点参数(按 type 解释:SET_CAMERA [0]=FOV度 [1]=near [2]=far;CREATE_DIRECTIONAL_LIGHT [0]=能量) */
	unigo_transform transform; /* 变换(仅 SET_INSTANCE_TRANSFORM 用) */
} unigo_render_command;

/* 回传映射:apply 后,宿主按命令序号读取(request_id → 真实 id;非创建命令填 0)。 */
typedef struct unigo_handle_result {
	uint64_t request_id;  /* 宿主请求标记 */
	uint64_t handle;      /* 后端分配的真实 id(创建命令);非创建命令填 0 */
} unigo_handle_result;

/**
 * 初始化渲染场景(创建 scenario/viewport/camera;不建默认灯,光源由 C# 经 CREATE_DIRECTIONAL_LIGHT 创建)。
 * 在 create 后、首次 apply 前调用一次。
 * @return 0=成功;负值=错误码。
 */
UNIGO_API int32_t unigo_render_setup(unigo_handle p_handle);

/**
 * 消费一批渲染命令(每批调用,批量驱动 RenderingServer;一帧可能多个批次)。
 * @param p_cmds 命令数组;@param p_count 命令数。
 * @return 0=成功;负值=错误码。
 */
UNIGO_API int32_t unigo_render_apply(unigo_handle p_handle, const unigo_render_command *p_cmds, int32_t p_count, unigo_handle_result *p_results);

/**
 * 取最后一次错误的诊断字符串(UTF-8,线程局部)。
 * 返回的指针在下次调用前有效,宿主应尽快复制。
 */
UNIGO_API const char *unigo_last_error(void);

#ifdef __cplusplus
}
#endif // __cplusplus
