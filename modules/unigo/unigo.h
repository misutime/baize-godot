/**************************************************************************/
/*  unigo.h                                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         UNIGO ENGINE (Fork)                            */
/*                    https://github.com/misutime/unigo-godot-kernel      */
/**************************************************************************/
/* UniGo 宿主与 Godot Kernel 之间的窄 C ABI(总体架构 §8 Native Bridge)。 */
/* 职责:稳定隔离 Godot 变化,导出入口少而稳定;宿主(C# EditorHost)只经   */
/* 本层驱动内核的初始化 / tick / shutdown,参数一律定宽整数、POD、UTF-8   */
/* 与不透明 Handle,每次调用返回明确错误码,不让异常穿越 ABI。            */
/*                                                                        */
/* 本头文件仅声明 C ABI;实现见 unigo.cpp(内部封装 GodotInstance + Main)。*/
/* 编译为 EDITOR_NATIVE_DLL(shared_library + editor_native)时导出。       */
/**************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* ---- 导出宏:与 libgodot.h 相同的可见性策略(EDITOR_NATIVE_DLL 时 DLL 导出) ---- */
#if defined(_MSC_VER) || defined(__MINGW32__)
#define UNIGO_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define UNIGO_API __attribute__((visibility("default")))
#else
#define UNIGO_API
#endif

/* ---- 统一错误码(与 UniGo.Godot.Contracts 的 ErrorCode 保持同一语义序) ---- */
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
	const char *project_path;   /* Godot 项目路径(可选,传 NULL 用内置默认) */
	const char *execpath;       /* 宿主可执行文件路径(必填,Main::setup 需要) */
	const char **argv;          /* 命令行参数(可选) */
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
 * 幂等;可在任意线程调用。
 */
UNIGO_API int32_t unigo_engine_request_exit(unigo_handle p_handle);

/**
 * 销毁内核实例(内部 Main::cleanup)。幂等;句柄此后失效。
 */
UNIGO_API void unigo_engine_shutdown(unigo_handle p_handle);

/**
 * 查询当前平台/后端是否支持窗口渲染(无图形环境返回 0)。
 * 供宿主决定是否创建窗口。
 */
UNIGO_API int32_t unigo_engine_query_render_support(void);

/**
 * 取最后一次错误的诊断字符串(UTF-8,线程局部)。
 * 返回的指针在下次调用前有效,宿主应尽快复制。
 */
UNIGO_API const char *unigo_last_error(void);

#ifdef __cplusplus
}
#endif // __cplusplus
