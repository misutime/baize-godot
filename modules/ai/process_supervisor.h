/**************************************************************************/
/*  process_supervisor.h                                                  */
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

#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/typedefs.h"

// S1 前置（审查修订 P1-1）：`OS::create_process` 无 per-spawn env/stdio/进程树能力
// （core/os/os.h:216-220、platform/windows/os_windows.cpp:1554-1620），无法满足
// sidecar spawn 合同（env 传 token/url、stdout/stderr 落日志、进程树清理）。
// 本类补齐三件事，不改 OS 抽象、隔离在 modules/ai：
//   1. env 增量注入（相对父进程环境，不经全局环境、不经 argv——token 不落 argv/日志）；
//   2. cwd 指定 + stdout/stderr 重定向到文件（sidecar 日志 user://logs/sidecar.log）；
//   3. 进程树 ownership：Windows = Job Object（KILL_ON_JOB_CLOSE，关闭即杀整树），
//      macOS/Unix = setsid 新会话 + killpg 杀进程组。
//
// 用法（SidecarServer）：spawn → 周期 is_running 检测 → 断开/退出时 kill_tree →
// 编辑器退出时 release（Windows 侧同时触发 Job 关闭杀树）。
class ProcessSupervisor {
public:
	struct SpawnOptions {
		String path;
		List<String> args; // 不得含 token（argv 泄露防护，§4.4）
		HashMap<String, String> env; // 增量环境变量（相对父进程，同名覆盖）
		String cwd; // 空 = 继承父进程 cwd
		String stdout_file; // 空 = 继承；否则子进程 stdout 追加写入该文件
		String stderr_file;
	};

	struct ProcessHandle {
		ProcessID pid = 0;
		uint64_t spawn_ms = 0; // 用于崩溃重启退避（稳定运行计时）
#ifdef _WIN32
		void *job = nullptr; // HANDLE：Job Object（KILL_ON_JOB_CLOSE）
		void *proc_handle = nullptr; // HANDLE：hProcess
#endif
		int exit_code = 0;
		bool reaped = false; // Unix：waitpid 已回收（防 zombie）
	};

	/// spawn 子进程；失败返回 ERR_CANT_FORK（r_handle 保持无效）。
	static Error spawn(const SpawnOptions &p_opts, ProcessHandle &r_handle);
	/// 终止整个进程树（含子进程再派生的 worker）：Windows TerminateJobObject；
	/// Unix kill(-pid, SIGTERM) → 500ms → kill(-pid, SIGKILL)。
	static Error kill_tree(const ProcessHandle &p_handle);
	/// 非阻塞存活检测；进程已退出时同步 reap（Unix 写 reaped/exit_code，故参数非 const）。
	static bool is_running(ProcessHandle &p_handle);
	/// 退出码（进程已退出后有效）。
	static int get_exit_code(const ProcessHandle &p_handle);
	/// 释放句柄/回收 zombie。Windows 侧 CloseHandle(job) 触发 KILL_ON_JOB_CLOSE（若进程仍存活）。
	static void release(ProcessHandle &r_handle);

private:
#ifndef _WIN32
	/// execve 不搜 PATH：把裸可执行名解析为绝对路径（默认 "node" 否则 ENOENT——审查 P1）。找不到返回空。
	static String _resolve_in_path(const String &p_name);
#endif
#ifdef _WIN32
	/// 构建 Windows 环境块（UTF-16LE，"K=V\0...\0"）：父进程环境 + 增量覆盖（同名大小写不敏感）。
	static Vector<Char16String> _build_env_block(const HashMap<String, String> &p_env);
	/// 打开日志文件为子进程句柄（可继承；bInheritHandles 下继承）。
	static void *_open_log_handle(const String &p_path);
#endif
};
