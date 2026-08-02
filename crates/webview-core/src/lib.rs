//! 引擎 webview Rust 核心（4A 方案）。
//!
//! M0 骨架：C ABI 契约桩（wv_create / wv_destroy / wv_pump）。
//! CEF 初始化/浏览器/OSR/IPC/IME 将在 M1-M2 接入（vendor cef_app + software_render，
//! 抽取 gdcef 的 godot 无关逻辑）。
//!
//! 边界纪律：本 crate 不依赖 Godot，只经 C ABI 与 C++ 壳（modules/webview/）通信。

use std::ffi::{c_char, c_int, c_void};

/// 非 panic 的 stderr 日志。
/// extern "C" 导出边界内禁止 eprintln!——stderr 写失败会 panic，跨 FFI 中止整个进程。
fn log_stderr(msg: &str) {
    use std::io::Write;
    let _ = std::io::stderr().write_all(msg.as_bytes());
}

/// 核心句柄（不透明；M0 为空壳，M1 起持有 CEF 初始化状态）。
#[repr(C)]
pub struct WebViewCore {
    _private: (),
}

/// 回调集合（C++ 侧实现）。
#[repr(C)]
pub struct WvCallbacks {
    pub on_paint: Option<unsafe extern "C" fn(*mut c_void, c_int, *const u8, u32, u32)>,
    pub on_message: Option<unsafe extern "C" fn(*mut c_void, c_int, *const c_char)>,
    pub on_load_status: Option<unsafe extern "C" fn(*mut c_void, c_int, c_int, *const c_char)>,
}

#[unsafe(no_mangle)]
pub extern "C" fn wv_create(_exe_dir: *const c_char, _cb: *const WvCallbacks, _ud: *mut c_void) -> *mut WebViewCore {
    // M0：仅分配句柄；M1 接入 CEF 初始化。
    log_stderr("[webview-core] wv_create (M0 skeleton)\n");
    Box::into_raw(Box::new(WebViewCore { _private: () }))
}

#[unsafe(no_mangle)]
pub extern "C" fn wv_destroy(core: *mut WebViewCore) {
    if core.is_null() {
        return;
    }
    log_stderr("[webview-core] wv_destroy\n");
    // M0：仅释放句柄；M1 起在此做 CEF 关闭。
    unsafe { drop(Box::from_raw(core)) };
}

#[unsafe(no_mangle)]
pub extern "C" fn wv_pump(_core: *mut WebViewCore) {
    // M0：空转；M1 起接入 CEF 消息泵（do_message_loop_work）。
}
