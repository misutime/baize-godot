/**************************************************************************/
/*  registry.h                                                   */
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

#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

// 编辑器能力注册表（editor_ops 唯一事实源，见《实施原则-编辑器领域能力统一editor_ops.md》）。
//
// 能力方法（ui.* / editor.* / scene.*）的唯一事实源：方法名、描述、参数 schema
// （JSON Schema，含 required）与实现 handler 集中注册。各暴露通道（nodejs_sidecar
// 的 WS/JSON-RPC、webview WebBridge 委托、未来 Node MCP）均从注册表生成/查询，避免
// 分发表与元数据表双份维护导致漂移（如 scene.create_node 默认名曾分叉）。
//
// 线程：全部 handler 在主线程调用（编辑器帧泵）。返回统一
// { ok, result } / { ok:false, error:{code,message} }（与 WebBridge 协议一致）。
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
