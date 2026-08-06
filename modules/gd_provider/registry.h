// SPDX-License-Identifier: MIT
#pragma once

/**************************************************************************/
/*  registry.h                                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

// 能力注册表（Godot Provider 的 Registry 层）：方法名/描述/参数 schema/错误码/事件声明
// 的唯一事实源。任何对外通道（当前 WS/JSON-RPC）从本表查询分派——能力实现只写一份。
//
// 返回语义（与 TS 侧 @baize/godot-rpc 契约对齐）：handler 返回
// { ok, result } / { ok:false, error:{ code, message } }，JSON-RPC 映射在 ProviderServer 层。
//
// 线程：全部 handler 在主线程（编辑器帧泵）调用。
class Registry {
public:
	typedef Dictionary (*Handler)(const Dictionary &p_args);

	struct Method {
		String name;
		String description;
		Dictionary input_schema; // JSON Schema（object，含 properties/required）
		Handler handler;
	};

	/// 惰性注册全部能力（幂等；首查时执行）。
	static void ensure_registered();
	/// 按方法名查询（未注册返回 nullptr）。
	static const Method *find(const String &p_name);
	/// 全部已注册方法（tools/list 数据源）。
	static const Vector<Method> &methods();
	/// 分发前参数校验：必须是对象 + 满足 input_schema.required。失败返回 false 并给出原因。
	static bool validate_args(const Method &p_method, const Variant &p_params, Dictionary &r_args, String &r_err);

private:
	static void register_method(const String &p_name, const String &p_desc, const Dictionary &p_schema, Handler p_handler);
	static void _register_all();

	static Vector<Method> s_methods;
	static bool s_registered;
};
