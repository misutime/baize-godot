/**************************************************************************/
/*  webview_runtime_path.h                                                */
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
/* included in all substantial copies or portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef WEBVIEW_RUNTIME_PATH_H
#define WEBVIEW_RUNTIME_PATH_H

#include "core/os/os.h"
#include "core/string/ustring.h"

// UI/字体根目录 = 其下存在 webview/ui/（页面）与 webview/ui/fonts/（默认字体）的目录。
// 分发契约(stage_webview.py):
//   - 非 bundle 裸可执行文件(bin/godot.macos.editor.dev.arm64):UI 与 exe 同级
//     (bin/webview/ui,stage_ui 暂存),根 = exe_dir;
//   - .app bundle 内 exe(bin/godot_macos_editor_dev.app/Contents/MacOS/Godot):UI/字体
//     打进 bundle 内 Contents/Resources/webview/ui(stage_bundles 暂存),根 =
//     <bundle>/Contents/Resources。
// Windows 无 bundle,恒为 exe_dir。
// 注意:webview_core.cpp(不 include Godot 头)对 CEF 运行时(framework/helper)同规则
// 解析到 Contents/Frameworks,改本契约需同步。
inline String webview_ui_root_dir() {
	String root = OS::get_singleton()->get_executable_path().get_base_dir();
	const String kContentsMacOS = "/Contents/MacOS";
	if (root.ends_with(kContentsMacOS)) {
		// bundle 根 = exe_dir 去掉 /Contents/MacOS 后缀（不用 ".." 拼，避免折叠算术出错）
		root = root.substr(0, root.length() - kContentsMacOS.length())
					   .path_join("Contents")
					   .path_join("Resources");
	}
	return root;
}

#endif // WEBVIEW_RUNTIME_PATH_H
