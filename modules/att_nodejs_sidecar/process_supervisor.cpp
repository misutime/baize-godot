/**************************************************************************/
/*  process_supervisor.cpp                                                */
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

#include "process_supervisor.h"

#include "core/string/print_string.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// 前向声明：_inherit_std_handle 定义在文件尾部，spawn（文件前部）先使用（遗留 P2 stdio 继承）。
static HANDLE _inherit_std_handle(DWORD p_std);
#else
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

Error ProcessSupervisor::spawn(const SpawnOptions &p_opts, ProcessHandle &r_handle) {
	ERR_FAIL_COND_V(p_opts.path.is_empty(), ERR_INVALID_PARAMETER);
	// argv 泄露防护：token 等机密只经 env 传递，禁止出现在 args。
	for (const String &arg : p_opts.args) {
		ERR_FAIL_COND_V_MSG(arg.contains("BAIZE_GODOT_TOKEN") || arg.contains("BAIZE_SIDECAR_TOKEN"), ERR_INVALID_PARAMETER,
				"ProcessSupervisor: 机密不得经 argv 传递（§4.4），请用 env");
	}

#ifdef _WIN32
	String command = "\"" + p_opts.path.replace("/", "\\") + "\"";
	for (const String &arg : p_opts.args) {
		command += " \"" + arg.replace("\"", "\\\"") + "\"";
	}

	// 环境块：父进程环境 + 增量覆盖（不修改 Godot 全局环境，§4.4）。
	Vector<Char16String> env_block = _build_env_block(p_opts.env);

	// stdio 重定向（仅 use_std_handles 时 bInheritHandles=TRUE，避免继承全部可继承句柄——审查 P2）。
	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	bool use_std_handles = !p_opts.stdout_file.is_empty() || !p_opts.stderr_file.is_empty();
	HANDLE h_in = nullptr, h_out = nullptr, h_err = nullptr;
	bool own_out = false, own_err = false; // 继承的父句柄不能 CloseHandle（遗留 P2）
	if (use_std_handles) {
		si.dwFlags |= STARTF_USESTDHANDLES;
		// STARTF_USESTDHANDLES 要求句柄可继承。
		// 指定流 → 打开可继承文件句柄；未指定流 → 继承父进程句柄（SetHandleInformation 设为可继承，与 Unix 语义一致——遗留 P2）；
		// 继承失败兜底 NUL（不静默用不可继承句柄直传 → 87）。
		h_in = (HANDLE)_open_log_handle(String("NUL")); // sidecar 无 stdin 需求
		if (p_opts.stdout_file.is_empty()) {
			h_out = _inherit_std_handle(STD_OUTPUT_HANDLE);
		} else {
			h_out = (HANDLE)_open_log_handle(p_opts.stdout_file);
			own_out = true;
		}
		if (p_opts.stderr_file.is_empty()) {
			h_err = _inherit_std_handle(STD_ERROR_HANDLE);
		} else {
			h_err = (HANDLE)_open_log_handle(p_opts.stderr_file);
			own_err = true;
		}
		if (!h_out) {
			h_out = (HANDLE)_open_log_handle(String("NUL"));
			own_out = true;
		}
		if (!h_err) {
			h_err = (HANDLE)_open_log_handle(String("NUL"));
			own_err = true;
		}
		if (!h_in || !h_out || !h_err) {
			ERR_PRINT("[Sidecar] ProcessSupervisor::spawn stdio 句柄打开失败（NUL/日志文件不可访问）");
			if (h_in) CloseHandle(h_in);
			if (h_out && own_out) CloseHandle(h_out);
			if (h_err && own_err) CloseHandle(h_err);
			return ERR_CANT_OPEN;
		}
		si.hStdInput = h_in;
		si.hStdOutput = h_out;
		si.hStdError = h_err;
	}

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));

	Char16String cwd_wide;
	if (!p_opts.cwd.is_empty()) {
		cwd_wide = p_opts.cwd.utf16();
	}

	// 环境块：拼接 "K=V\0K2=V2\0\0"（UTF-16）。CharStringT::size() 含结尾 null，必须用 length()（否则条目间双 null 截断 env 块）。
	Vector<char16_t> env_data;
	for (const Char16String &entry : env_block) {
		for (int i = 0; i < entry.length(); i++) {
			env_data.push_back(entry[i]);
		}
		env_data.push_back(0);
	}
	env_data.push_back(0);

	// 显式环境块（lpEnvironment 非空）必须带 CREATE_UNICODE_ENVIRONMENT，否则 CreateProcessW 返回 ERROR_INVALID_PARAMETER 87（实测）。
	// CREATE_SUSPENDED：Job Object 绑定成功前不放行（进程可能在绑定前派生 worker——审查 P1）。
	DWORD creation_flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
	int ret = CreateProcessW(
			nullptr,
			(LPWSTR)(command.utf16().ptrw()),
			nullptr,
			nullptr,
			use_std_handles, // bInheritHandles：仅重定向时继承（审查 P2 句柄面）
			creation_flags,
			env_data.is_empty() ? nullptr : (LPVOID)env_data.ptrw(),
			cwd_wide.is_empty() ? nullptr : (LPCWSTR)cwd_wide.ptrw(),
			&si,
			&pi);
	if (ret == 0) {
		ERR_PRINT("[Sidecar] ProcessSupervisor::spawn 失败: " + command + " err=" + itos(GetLastError()));
		if (h_in) CloseHandle(h_in);
		if (h_out && own_out) CloseHandle(h_out);
		if (h_err && own_err) CloseHandle(h_err);
		return ERR_CANT_FORK;
	}

	// Job Object：KILL_ON_JOB_CLOSE —— 句柄关闭（release/崩溃路径）即杀整树。
	// 配置/绑定失败不静默降级（进程树 kill 合同，审查 P1）：终止进程并报错。
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	bool job_ok = false;
	if (job != nullptr) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
		ZeroMemory(&jeli, sizeof(jeli));
		jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
			if (AssignProcessToJobObject(job, pi.hProcess)) {
				job_ok = true;
			} else {
				ERR_PRINT("[Sidecar] ProcessSupervisor: AssignProcessToJobObject 失败 err=" + itos(GetLastError()) + "（进程树 kill 不可用）");
			}
		} else {
			ERR_PRINT("[Sidecar] ProcessSupervisor: SetInformationJobObject 失败 err=" + itos(GetLastError()));
		}
		if (!job_ok) {
			CloseHandle(job);
			job = nullptr;
		}
	}
	if (!job_ok) {
		ERR_PRINT("[Sidecar] ProcessSupervisor: Job Object 配置失败，终止 spawn（不静默降级）");
		// 遗留 P2：确认终止成功；失败时保留进程句柄供后续清理（否则挂起孤儿进程无句柄可杀）。
		if (!TerminateProcess(pi.hProcess, 1)) {
			ERR_PRINT("[Sidecar] ProcessSupervisor: TerminateProcess 失败 err=" + itos(GetLastError()) + "（进程可能残留）");
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		if (h_in) CloseHandle(h_in);
		if (h_out && own_out) CloseHandle(h_out);
		if (h_err && own_err) CloseHandle(h_err);
		return ERR_CANT_FORK;
	}
	if (ResumeThread(pi.hThread) == (DWORD)-1) {
		// 遗留 P2：恢复线程失败 → 进程永久挂起且 is_running 恒 true——终止并报错。
		ERR_PRINT("[Sidecar] ProcessSupervisor: ResumeThread 失败 err=" + itos(GetLastError()) + "（终止挂起进程）");
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(job);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		if (h_in) CloseHandle(h_in);
		if (h_out && own_out) CloseHandle(h_out);
		if (h_err && own_err) CloseHandle(h_err);
		return ERR_CANT_FORK;
	}
	// 父侧 stdio 句柄关闭（子进程已继承副本——审查 P2 句柄泄漏；继承的父句柄不关）。
	if (h_in) CloseHandle(h_in);
	if (h_out && own_out) CloseHandle(h_out);
	if (h_err && own_err) CloseHandle(h_err);

	r_handle.pid = pi.dwProcessId;
	r_handle.job = job;
	r_handle.proc_handle = pi.hProcess;
	r_handle.spawn_ms = OS::get_singleton()->get_ticks_msec();
	CloseHandle(pi.hThread);
	return OK;
#else // !_WIN32
	// execve 不搜 PATH：裸可执行名先在 PATH 中解析绝对路径（默认 "node" 否则 ENOENT——审查 P1）。
	String exec_path = p_opts.path;
	if (!exec_path.contains("/")) {
		exec_path = _resolve_in_path(exec_path);
		if (exec_path.is_empty()) {
			ERR_PRINT("[Sidecar] ProcessSupervisor: 在 PATH 中找不到可执行文件: " + p_opts.path);
			return ERR_FILE_NOT_FOUND;
		}
	}
	pid_t pid = fork();
	ERR_FAIL_COND_V_MSG(pid < 0, ERR_CANT_FORK, "ProcessSupervisor::spawn fork 失败");

	if (pid == 0) {
		// 子进程：新会话（进程组 id = pid，killpg 可杀整树）。
		setsid();

		if (!p_opts.cwd.is_empty() && chdir(p_opts.cwd.utf8().get_data()) != 0) {
			// cwd 合同：失败 = spawn 失败，不静默继续（审查 P2）。
			fprintf(stderr, "[sidecar] chdir 失败: %s\n", p_opts.cwd.utf8().get_data());
			_exit(1);
		}

		if (!p_opts.stdout_file.is_empty()) {
			int fd = open(p_opts.stdout_file.utf8().get_data(), O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) {
				// 日志重定向失败：终止（不静默继承 Godot 描述符——审查 P2）。
				fprintf(stderr, "[sidecar] stdout 重定向失败: %s\n", p_opts.stdout_file.utf8().get_data());
				_exit(1);
			}
			close(fd);
		}
		if (!p_opts.stderr_file.is_empty()) {
			int fd = open(p_opts.stderr_file.utf8().get_data(), O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (fd < 0 || dup2(fd, STDERR_FILENO) < 0) {
				fprintf(stderr, "[sidecar] stderr 重定向失败: %s\n", p_opts.stderr_file.utf8().get_data());
				_exit(1);
			}
			close(fd);
		}

		// envp：父环境 + 增量覆盖（同名替换）。
		HashMap<String, String> merged;
		for (char **e = environ; *e; e++) {
			String entry = String::utf8(*e);
			int eq = entry.find("=");
			if (eq > 0) {
				merged[entry.substr(0, eq)] = entry.substr(eq + 1);
			}
		}
		for (const KeyValue<String, String> &kv : p_opts.env) {
			merged[kv.key] = kv.value;
		}
		Vector<CharString> envp;
		Vector<const char *> envp_ptrs;
		for (const KeyValue<String, String> &kv : merged) {
			envp.push_back((kv.key + "=" + kv.value).utf8());
		}
		for (const CharString &cs : envp) {
			envp_ptrs.push_back(cs.get_data());
		}
		envp_ptrs.push_back(nullptr);

		Vector<CharString> cs;
		cs.push_back(p_opts.path.utf8());
		for (const String &arg : p_opts.args) {
			cs.push_back(arg.utf8());
		}
		Vector<char *> args;
		for (int i = 0; i < cs.size(); i++) {
			args.push_back((char *)cs[i].get_data());
		}
		args.push_back(nullptr);

		execve(exec_path.utf8().get_data(), args.ptrw(), (char *const *)envp_ptrs.ptrw());
		fprintf(stderr, "[sidecar] execve 失败: %s (errno=%d)\n", exec_path.utf8().get_data(), errno);
		raise(SIGKILL);
	}

	r_handle.pid = pid;
	r_handle.spawn_ms = OS::get_singleton()->get_ticks_msec();
	return OK;
#endif
}

Error ProcessSupervisor::kill_tree(const ProcessHandle &p_handle) {
	if (p_handle.pid == 0) {
		return ERR_INVALID_PARAMETER;
	}
#ifdef _WIN32
	if (p_handle.job != nullptr) {
		// Job Object：一次终止整个作业内进程树。
		return TerminateJobObject((HANDLE)p_handle.job, 1) ? OK : FAILED;
	}
	if (p_handle.proc_handle != nullptr) {
		return TerminateProcess((HANDLE)p_handle.proc_handle, 1) ? OK : FAILED;
	}
	return FAILED;
#else
	// 进程组（setsid 后 pgid == pid）：先 SIGTERM 优雅，500ms 后 SIGKILL。
	::kill(-p_handle.pid, SIGTERM);
	OS::get_singleton()->delay_usec(500000);
	::kill(-p_handle.pid, SIGKILL);
	return OK;
#endif
}

bool ProcessSupervisor::is_running(ProcessHandle &p_handle) {
	if (p_handle.pid == 0) {
		return false;
	}
#ifdef _WIN32
	if (p_handle.proc_handle == nullptr) {
		return false;
	}
	return WaitForSingleObject((HANDLE)p_handle.proc_handle, 0) == WAIT_TIMEOUT;
#else
	if (p_handle.reaped) {
		return false;
	}
	int status = 0;
	pid_t r;
	do {
		r = waitpid(p_handle.pid, &status, WNOHANG);
	} while (r < 0 && errno == EINTR); // EINTR 重试，防止把未回收的子进程误标 reaped（审查 P2）
	if (r == p_handle.pid) {
		p_handle.reaped = true;
		p_handle.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		return false;
	}
	return true;
#endif
}

int ProcessSupervisor::get_exit_code(const ProcessHandle &p_handle) {
#ifdef _WIN32
	if (p_handle.proc_handle == nullptr) {
		return -1;
	}
	DWORD code = 0;
	GetExitCodeProcess((HANDLE)p_handle.proc_handle, &code);
	return (int)code;
#else
	return p_handle.exit_code;
#endif
}

void ProcessSupervisor::release(ProcessHandle &r_handle) {
	if (r_handle.pid == 0) {
		return;
	}
#ifdef _WIN32
	// 先关 Job（KILL_ON_JOB_CLOSE：若进程仍在则杀整树），再关进程句柄。
	if (r_handle.job != nullptr) {
		CloseHandle((HANDLE)r_handle.job);
		r_handle.job = nullptr;
	}
	if (r_handle.proc_handle != nullptr) {
		CloseHandle((HANDLE)r_handle.proc_handle);
		r_handle.proc_handle = nullptr;
	}
#else
	if (!r_handle.reaped) {
		int status = 0;
		pid_t r;
		do {
			r = waitpid(r_handle.pid, &status, 0);
		} while (r < 0 && errno == EINTR); // EINTR 重试，只在确认回收后更新状态（审查 P2）
		if (r == r_handle.pid) {
			r_handle.reaped = true;
			r_handle.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		}
	}
#endif
	r_handle.pid = 0;
}

#ifndef _WIN32
String ProcessSupervisor::_resolve_in_path(const String &p_name) {
	// execve 不搜 PATH：按 PATH 逐目录找可执行文件（审查 P1）。
	// 遗留 P2：PATH 可能含相对目录（如 "."、"bin"）——子进程 chdir 后相对路径失效，
	// 统一以父进程 cwd 为基准转绝对路径（access 检查与 execve 执行口径一致）。
	const char *env_path = std::getenv("PATH");
	if (!env_path) {
		return String();
	}
	char cwd_buf[4096];
	const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf));
	const Vector<String> dirs = String::utf8(env_path).split(":");
	for (const String &dir : dirs) {
		String abs_dir = dir;
		if (dir.is_empty()) {
			abs_dir = cwd ? String::utf8(cwd) : String("."); // 空 PATH 项 = 当前目录
		} else if (!dir.begins_with("/")) {
			abs_dir = (cwd ? String::utf8(cwd) : String(".")) + "/" + dir;
		}
		const String candidate = abs_dir.path_join(p_name);
		if (::access(candidate.utf8().get_data(), X_OK) == 0) {
			return candidate;
		}
	}
	return String();
}
#endif

#ifdef _WIN32
// 前向声明（定义在文件尾部 _open_log_handle 之后）。
static HANDLE _inherit_std_handle(DWORD p_std);

// wchar_t*（UTF-16，Windows）→ 内部 String（char32_t 码点），合并代理对。
static String _from_wchar(const wchar_t *p_str) {
	Vector<char32_t> buf;
	for (const wchar_t *p = p_str; *p; p++) {
		char32_t c = (char32_t)*p;
		if (c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {
			c = 0x10000 + ((c - 0xD800) << 10) + ((char32_t)p[1] - 0xDC00);
			p++;
		}
		buf.push_back(c);
	}
	buf.push_back(0);
	return String(buf.ptrw());
}

Vector<Char16String> ProcessSupervisor::_build_env_block(const HashMap<String, String> &p_env) {
	Vector<Char16String> block;
	// 父进程环境逐条拷贝（同名条目被增量覆盖时跳过；追加在后，CreateProcessW 取后者）。
	// Windows 环境变量名不区分大小写：覆盖判断用 to_lower 比较（审查 P2——Path/PATH 重复）。
	LPWCH parent_env = GetEnvironmentStringsW();
	for (LPWCH e = parent_env; e && *e; e += wcslen(e) + 1) {
		String entry = _from_wchar(e);
		int eq = entry.find("=");
		if (eq > 0) {
			bool overridden = false;
			const String key = entry.substr(0, eq);
			for (const KeyValue<String, String> &kv : p_env) {
				if (kv.key.to_lower() == key.to_lower()) {
					overridden = true;
					break;
				}
			}
			if (!overridden) {
				block.push_back(entry.utf16());
			}
		}
	}
	FreeEnvironmentStringsW(parent_env);
	for (const KeyValue<String, String> &kv : p_env) {
		block.push_back((kv.key + "=" + kv.value).utf16());
	}
	return block;
}

void *ProcessSupervisor::_open_log_handle(const String &p_path) {
	// 子进程继承需要 bInheritHandle=TRUE（否则 CreateProcessW STARTF_USESTDHANDLES 报 ERROR_INVALID_PARAMETER 87）。
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = nullptr;
	sa.bInheritHandle = TRUE;
	HANDLE h = CreateFileW(
			(LPCWSTR)p_path.utf16().ptrw(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			&sa,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
	return h == INVALID_HANDLE_VALUE ? nullptr : (void *)h;
}

// 遗留 P2：未指定流继承父进程句柄——GetStdHandle 的句柄默认不可继承，须 SetHandleInformation 设为可继承。
// 返回的句柄属父进程（调用方不得 CloseHandle）。
static HANDLE _inherit_std_handle(DWORD p_std) {
	HANDLE h = GetStdHandle(p_std);
	if (h != nullptr && h != INVALID_HANDLE_VALUE) {
		SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
		return h;
	}
	return nullptr;
}
#endif
