/**************************************************************************/
/*  ops.h                                                        */
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

class Control;

// 编辑器操作实现（editor_ops 能力面）。
//
// 操作以「语义目标」驱动，不模拟鼠标：
// - 引擎 API 直调：select_node / set_prop / undo / redo（走编辑器既有路径，undo 一致）；
// - 控件语义动作：activate（Button→真实输入路径 ui_accept、TreeItem→选中）、
//   set_text（LineEdit/TextEdit/SpinBox，只读控件拒绝）；
// - 通用回退：InputEvent 投递到目标 Control（坐标由引擎从布局计算）。
//
// 所有操作返回统一 { ok, result } / { ok:false, error:{code,message} }（与 WebBridge 协议一致）。
class Ops {
public:
	/// ui.activate：点击/激活语义目标（Button→ui_accept 真实路径、TreeItem→选中、回退 InputEvent 投递）。
	static Dictionary activate(const String &p_id);
	/// ui.set_text：文本/数值输入（LineEdit / TextEdit / SpinBox）。
	static Dictionary set_text(const String &p_id, const String &p_value);
	/// ui.focus：聚焦控件。
	static Dictionary focus(const String &p_id);
	/// ui.get_tree：语义 UI 树快照（EditorUiTree）。
	static Dictionary get_ui_tree();

	/// editor.select_node：选中场景节点（场景相对路径；"." = 根）。
	static Dictionary select_node(const String &p_path);
	/// editor.set_prop：设置场景节点属性（undo 入栈）。
	static Dictionary set_prop(const String &p_path, const String &p_prop, const Variant &p_value);
	/// editor.get_state：当前编辑器状态（场景/选中/undo 栈）。
	static Dictionary get_state();

	/// editor.undo / editor.redo：撤销/重做（编辑器 undo 栈，与人工一致）。
	static Dictionary undo();
	static Dictionary redo();
	/// scene.get_node_count：场景节点数（含根）。
	static Dictionary get_node_count();
	/// scene.create_node：创建 Node3D 子节点（undo 可撤销），返回 instance_id。
	static Dictionary create_node(const String &p_name);

	/// scene.get_node_position / scene.set_node_position：读取/设置 Node3D 位置（set undo 入栈）。
	static Dictionary get_node_position(const String &p_path);
	static Dictionary set_node_position(const String &p_path, const Dictionary &p_position);
	/// editor.get_ui_*：编辑器 UI 主题状态（读能力——WebDock 渲染对齐与外部排查共用）。
	static Dictionary get_ui_font_size();
	static Dictionary get_ui_scale();
	static Dictionary get_ui_font();
	static Dictionary get_ui_font_bold();
	/// 编辑器默认字体解析路径运行时存储（editor_fonts 写入；字体来源单一 = 编辑器，非持久化）。
	static void set_resolved_fonts(const String &p_regular, const String &p_bold);
	static String get_resolved_main_font(); // 默认字体实际路径（WebDock 事件推送等读取）
	static String get_resolved_main_font_bold();

	/// ui.activate 的通用回退：InputEvent 投递到 Control 中心（引擎算坐标）。
	static Dictionary _activate_input_fallback(Control *p_ctrl);

private:
	static String resolved_main_font_; // 默认字体实际路径（editor_fonts 写入）
	static String resolved_main_font_bold_; // 默认粗体实际路径（editor_fonts 写入）
};
