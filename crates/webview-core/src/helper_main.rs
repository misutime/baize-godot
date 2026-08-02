//! CEF 子进程入口（4A webview-helper）。
//!
//! 基于 godot-cef 的 gdcef_helper（MIT）改写：去 gdext 依赖、mac 部分后置。
//! CEF 多进程架构：主进程（编辑器）经 browser_subprocess_path 指定本可执行文件，
//! CEF 为 renderer/GPU 等子进程启动它。

#![cfg_attr(
    all(target_os = "windows", not(debug_assertions)),
    windows_subsystem = "windows"
)]

use cef::{api_hash, execute_process};
use cef::{CefString, ImplCommandLine, args::Args};

// Godot 在 Windows 上设置 NvOptimusEnablement / AmdPowerXpressRequestHighPerformance 以
// 请求混合显卡笔记本使用独显；helper 子进程同样设置，避免与主进程 GPU 不一致。
#[cfg(target_os = "windows")]
#[unsafe(no_mangle)]
#[used]
pub static NvOptimusEnablement: u32 = 0x00000001;

#[cfg(target_os = "windows")]
#[unsafe(no_mangle)]
#[used]
pub static AmdPowerXpressRequestHighPerformance: u32 = 0x00000001;

fn main() -> std::process::ExitCode {
    api_hash(cef::sys::CEF_API_VERSION_LAST, 0);

    let args = Args::new();
    let Some(cmd) = args.as_cmd_line() else {
        eprintln!("[webview-helper] Failed to parse CEF command line args");
        return std::process::ExitCode::FAILURE;
    };

    let switch = CefString::from("type");
    let is_browser_process = cmd.has_switch(Some(&switch)) != 1;
    let mut app = cef_app::AppBuilder::build(cef_app::OsrApp::new());
    let ret = execute_process(
        Some(args.as_main_args()),
        Some(&mut app),
        std::ptr::null_mut(),
    );

    if is_browser_process {
        if ret != -1 {
            eprintln!("[webview-helper] cannot execute browser process");
            return std::process::ExitCode::FAILURE;
        }
    } else {
        let process_type = CefString::from(&cmd.switch_value(Some(&switch)));
        println!("[webview-helper] launch process {process_type}");
        if ret < 0 {
            eprintln!("[webview-helper] cannot execute non-browser process");
            return std::process::ExitCode::FAILURE;
        }
        // 非 browser 子进程：execute_process 阻塞并返回子进程真实退出码，原样传播
        // （-1 是 browser 进程哨兵；正值失败不可被掩盖成 0）。
        return std::process::ExitCode::from(ret as u8);
    }

    std::process::ExitCode::SUCCESS
}
