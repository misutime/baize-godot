//! CEF 核心状态与 C ABI 实现（4A M1b）。
//!
//! 软件渲染路径：CEF OnPaint（BGRA）→ 转 RGBA → 经 `WvOnPaint` 回调交给 C++ 壳上传纹理。
//! 线程模型：CEF 在主线程初始化/泵送（external_message_pump），回调与 `wv_pump` 同线程
//! （编辑器主线程）——C++ 侧可安全做纹理操作。

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void, CStr};
use std::sync::Mutex;

use cef::{
    self, Browser, Client, ImplBrowser, ImplBrowserHost, ImplClient, ImplFrame,
    ImplLoadHandler, ImplRenderHandler, LoadHandler, RenderHandler, ScreenInfo, WindowInfo,
    WrapClient, WrapLoadHandler, WrapRenderHandler, rc::Rc, wrap_client, wrap_load_handler,
    wrap_render_handler,
};

use cef_app::{OsrApp, OsrRenderHandler, PhysicalSize};

// ---------- C ABI 类型（与 webview_ffi.h 对应） ----------

/// 回调集合（C++ 侧实现）。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WvCallbacks {
    pub on_paint: Option<unsafe extern "C" fn(*mut c_void, c_int, *const u8, u32, u32)>,
    pub on_message: Option<unsafe extern "C" fn(*mut c_void, c_int, *const c_char)>,
    pub on_load_status: Option<unsafe extern "C" fn(*mut c_void, c_int, c_int, *const c_char)>,
}

/// 回调 + 用户数据（可克隆进 render/load handler）。
#[derive(Clone, Copy)]
struct Callbacks {
    on_paint: Option<unsafe extern "C" fn(*mut c_void, c_int, *const u8, u32, u32)>,
    on_load_status: Option<unsafe extern "C" fn(*mut c_void, c_int, c_int, *const c_char)>,
    userdata: *mut c_void,
}

// 原始指针跨线程存储所需（CEF 回调与 pump 同线程，Mutex 保护静态状态）。
unsafe impl Send for Callbacks {}
unsafe impl Sync for Callbacks {}

/// 核心句柄（不透明）。
#[repr(C)]
pub struct WebViewCore {
    _private: (),
}

struct BrowserState {
    browser: cef::Browser,
    render_handler: cef::RenderHandler,
    size: std::sync::Arc<Mutex<PhysicalSize<f32>>>,
    closing: bool, // close_browser 已请求（防重复计数）
}

struct CoreState {
    callbacks: Callbacks,
    exe_dir: String,
    cef_initialized: bool,
    cef_failed: bool, // 初始化失败为终态：CEF 禁止失败后再次调用
    pending_close: usize, // 待异步关闭的浏览器数（OnBeforeClose 递减）
    browsers: HashMap<i32, BrowserState>,
}

static CORE: Mutex<Option<CoreState>> = Mutex::new(None);

fn core_lock() -> std::sync::MutexGuard<'static, Option<CoreState>> {
    match CORE.lock() {
        Ok(g) => g,
        Err(p) => p.into_inner(), // 中毒恢复：不静默丢弃状态
    }
}

fn log_stderr(msg: &str) {
    use std::io::Write;
    let _ = std::io::stderr().write_all(msg.as_bytes());
}

/// BGRA → RGBA（CEF 软件渲染输出 BGRA）。
fn bgra_to_rgba(bgra: &[u8]) -> Vec<u8> {
    let mut rgba = vec![0u8; bgra.len()];
    for (chunk, out) in bgra.chunks_exact(4).zip(rgba.chunks_exact_mut(4)) {
        out[0] = chunk[2];
        out[1] = chunk[1];
        out[2] = chunk[0];
        out[3] = chunk[3];
    }
    rgba
}

// ---------- CEF handler（轻量实现，无 godot 事件队列） ----------

wrap_render_handler! {
    pub struct WvOsrHandler {
        size: std::sync::Arc<Mutex<PhysicalSize<f32>>>,
        dpi: f32,
        cb: Callbacks,
        id: i32,
    }

    impl RenderHandler {
        fn view_rect(&self, _browser: Option<&mut Browser>, rect: Option<&mut cef::Rect>) {
            if let Some(rect) = rect {
                if let Ok(size) = self.size.lock() {
                    rect.x = 0;
                    rect.y = 0;
                    rect.width = size.width as i32;
                    rect.height = size.height as i32;
                }
            }
        }

        fn screen_info(&self, _browser: Option<&mut Browser>, screen_info: Option<&mut ScreenInfo>) -> c_int {
            if let Some(si) = screen_info {
                si.device_scale_factor = self.dpi;
                si.depth = 24;
                si.depth_per_component = 8;
                si.is_monochrome = 0;
            }
            1
        }

        fn on_paint(
            &self,
            _browser: Option<&mut Browser>,
            type_: cef::PaintElementType,
            _dirty_rects: Option<&[cef::Rect]>,
            buffer: *const u8,
            width: c_int,
            height: c_int,
        ) {
            // M1b：只处理主视图（popup 合成留后续）。
            if type_ != cef::PaintElementType::VIEW {
                return;
            }
            if buffer.is_null() || width <= 0 || height <= 0 {
                return;
            }
            let w = width as u32;
            let h = height as u32;
            let n = (w * h * 4) as usize;
            // CEF 输出 BGRA，契约要求 RGBA。
            let rgba = bgra_to_rgba(unsafe { std::slice::from_raw_parts(buffer, n) });
            if let Some(cb) = self.cb.on_paint {
                unsafe { cb(self.cb.userdata, self.id, rgba.as_ptr(), w, h) };
            }
        }
    }
}

// 加载状态观测（M1b 诊断用；M2 升级为页面事件）。
wrap_load_handler! {
    pub struct WvLoadHandler {
        cb: Callbacks,
        id: i32,
    }

    impl LoadHandler {
        fn on_load_end(&self, _browser: Option<&mut Browser>, _frame: Option<&mut cef::Frame>, http_status_code: c_int) {
            log_stderr(&format!("[webview-core] browser {} load end (status {})\n", self.id, http_status_code));
            if let Some(cb) = self.cb.on_load_status {
                unsafe { cb(self.cb.userdata, self.id, http_status_code, std::ptr::null()) };
            }
        }

        fn on_load_error(
            &self,
            _browser: Option<&mut Browser>,
            _frame: Option<&mut cef::Frame>,
            error_code: cef::Errorcode,
            error_text: Option<&cef::CefString>,
            _failed_url: Option<&cef::CefString>,
        ) {
            log_stderr(&format!(
                "[webview-core] browser {} load error: {:?} ({:?})\n",
                self.id, error_code, error_text
            ));
            if let Some(cb) = self.cb.on_load_status {
                unsafe { cb(self.cb.userdata, self.id, -1, std::ptr::null()) };
            }
        }
    }
}

wrap_client! {
    pub struct WvClient {
        render_handler: cef::RenderHandler,
        load_handler: cef::LoadHandler,
    }

    impl Client {
        fn render_handler(&self) -> Option<cef::RenderHandler> {
            Some(self.render_handler.clone())
        }

        fn load_handler(&self) -> Option<cef::LoadHandler> {
            Some(self.load_handler.clone())
        }
    }
}

// 构造器：wrap 宏生成 `new()`（按字段声明顺序）与 `From<..>` 包装转换。
// 字段顺序：size, dpi, cb, id。


// ---------- C ABI 导出 ----------

/// CEF 初始化（进程级一次，首次浏览器创建时惰性执行——godot-cef 验证过的时机：
/// 进程/消息循环已就绪；模块 SCENE 初始化阶段过早会导致崩溃）。
fn init_cef(exe_dir: &str) -> bool {
    // 必须先行：cef crate 的 wrap 宏以 api_hash 初始化全局 API 版本，
    // 缺失会导致所有包装结构体 version=-1（CefApp_0_CToCpp invalid version -1 崩溃）。
    cef::api_hash(cef::sys::CEF_API_VERSION_LAST, 0);

    let subprocess_path = if exe_dir.is_empty() {
        "webview-helper.exe".to_string()
    } else {
        format!("{}/webview-helper.exe", exe_dir)
    };

    // OsrApp：M1b 开启远程调试（编辑器 dev 环境）。
    let app_builder = OsrApp::builder().remote_debugging(true).remote_debugging_port(9229);
    let mut app = cef_app::AppBuilder::build(app_builder.build());

    let settings = cef::Settings {
        browser_subprocess_path: subprocess_path.as_str().into(),
        windowless_rendering_enabled: true as _,
        external_message_pump: true as _,
        log_severity: cef::LogSeverity::DEFAULT as _,
        root_cache_path: if exe_dir.is_empty() {
            "cef-data".into()
        } else {
            format!("{}/webview/cef-data", exe_dir).as_str().into()
        },
        ..Default::default()
    };

    let args = cef::args::Args::new();
    let ret = cef::initialize(
        Some(args.as_main_args()),
        Some(&settings),
        Some(&mut app),
        std::ptr::null_mut(),
    );
    if ret != 1 {
        log_stderr("[webview-core] CEF initialization failed\n");
        return false;
    }
    log_stderr("[webview-core] CEF initialized\n");
    true
}

/// 初始化 CEF（惰性：首次浏览器创建时）。`exe_dir`：编辑器可执行文件所在目录（bin/）。
#[unsafe(no_mangle)]
pub extern "C" fn wv_create(exe_dir: *const c_char, cb: *const WvCallbacks, userdata: *mut c_void) -> *mut WebViewCore {
    let mut state = core_lock();
    if state.is_some() {
        log_stderr("[webview-core] wv_create: already initialized\n");
        return std::ptr::null_mut();
    }

    let exe_dir = if exe_dir.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(exe_dir) }.to_string_lossy().into_owned()
    };
    // CEF 151 要求 browser_subprocess_path / root_cache_path 非空时为绝对路径——
    // 拒绝空 exe_dir，避免相对路径进入初始化。
    if exe_dir.is_empty() {
        log_stderr("[webview-core] wv_create: exe_dir required (absolute)\n");
        return std::ptr::null_mut();
    }

    let callbacks = if cb.is_null() {
        Callbacks {
            on_paint: None,
            on_load_status: None,
            userdata,
        }
    } else {
        let cb = unsafe { *cb };
        Callbacks {
            on_paint: cb.on_paint,
            on_load_status: cb.on_load_status,
            userdata,
        }
    };

    *state = Some(CoreState {
        callbacks,
        exe_dir,
        cef_initialized: false,
        cef_failed: false,
        pending_close: 0,
        browsers: HashMap::new(),
    });
    log_stderr("[webview-core] wv_create: core ready (CEF init deferred)\n");
    Box::into_raw(Box::new(WebViewCore { _private: () }))
}

/// 创建 OSR 浏览器（软件渲染）。
#[unsafe(no_mangle)]
pub extern "C" fn wv_create_browser(
    _core: *mut WebViewCore,
    id: c_int,
    url: *const c_char,
    width: u32,
    height: u32,
) -> c_int {
    let mut state = core_lock();
    let Some(state) = state.as_mut() else {
        log_stderr("[webview-core] wv_create_browser: core not initialized\n");
        return -1;
    };
    if !state.cef_initialized {
        if !init_cef(&state.exe_dir) {
            // 终态：CEF 初始化失败后禁止重试/再次调用。
            state.cef_failed = true;
            return -1;
        }
        state.cef_initialized = true;
    }
    if state.browsers.contains_key(&id) {
        log_stderr("[webview-core] wv_create_browser: id already exists\n");
        return -1;
    }
    if width == 0 || height == 0 {
        log_stderr("[webview-core] wv_create_browser: zero size\n");
        return -1;
    }
    let Some(url) = (if url.is_null() { None } else { unsafe { CStr::from_ptr(url) }.to_str().ok() }) else {
        log_stderr("[webview-core] wv_create_browser: invalid url\n");
        return -1;
    };

    let dpi = 1.0f32; // M1b：无 DPI 缩放处理，V2 接 DisplayServer 缩放
    let render_handler = OsrRenderHandler::new(dpi, PhysicalSize::new(width as f32, height as f32));
    let size = render_handler.get_size();

    let cef_render_handler: cef::RenderHandler = WvOsrHandler::new(size.clone(), dpi, state.callbacks, id).into();
    let cef_load_handler: cef::LoadHandler = WvLoadHandler::new(state.callbacks, id).into();
    let mut client: cef::Client = WvClient::new(cef_render_handler.clone(), cef_load_handler).into();

    let window_info = WindowInfo {
        bounds: cef::Rect { x: 0, y: 0, width: width as i32, height: height as i32 },
        windowless_rendering_enabled: true as _,
        shared_texture_enabled: false as _,
        external_begin_frame_enabled: true as _,
        ..Default::default()
    };
    let browser_settings = cef::BrowserSettings {
        windowless_frame_rate: 60,
        ..Default::default()
    };
    let cef_url: cef::CefStringUtf16 = url.into();
    let browser = match cef::browser_host_create_browser_sync(
        Some(&window_info),
        Some(&mut client),
        Some(&cef_url),
        Some(&browser_settings),
        None, // extra_info：M1b 无需
        None, // request_context：默认（无自定义 scheme）
    ) {
        Some(b) => b,
        None => {
            log_stderr("[webview-core] wv_create_browser: browser creation failed\n");
            return -1;
        }
    };

    state.browsers.insert(
        id,
        BrowserState {
            browser,
            render_handler: cef_render_handler,
            size,
            closing: false,
        },
    );
    0
}

/// 导航到新 URL（运行期 set_url 传播）。
#[unsafe(no_mangle)]
pub extern "C" fn wv_navigate_browser(_core: *mut WebViewCore, id: c_int, url: *const c_char) -> c_int {
    let state = core_lock();
    let Some(state) = state.as_ref() else {
        return -1;
    };
    let Some(bs) = state.browsers.get(&id) else {
        return -1;
    };
    let Some(url) = (if url.is_null() {
        None
    } else {
        unsafe { CStr::from_ptr(url) }.to_str().ok()
    }) else {
        return -1;
    };
    if let Some(mut frame) = bs.browser.main_frame() {
        let u: cef::CefString = url.into();
        frame.load_url(Some(&u));
        0
    } else {
        -1
    }
}

/// 调整浏览器尺寸（物理像素）。
#[unsafe(no_mangle)]
pub extern "C" fn wv_resize_browser(_core: *mut WebViewCore, id: c_int, width: u32, height: u32) -> c_int {
    let state = core_lock();
    let Some(state) = state.as_ref() else {
        return -1;
    };
    let Some(bs) = state.browsers.get(&id) else {
        return -1;
    };
    if let Ok(mut size) = bs.size.lock() {
        size.width = width as f32;
        size.height = height as f32;
    }
    if let Some(mut host) = bs.browser.host() {
        host.was_resized();
    }
    0
}

/// 销毁浏览器（异步关闭；状态保留至 OnBeforeClose 从 map 移除）。
#[unsafe(no_mangle)]
pub extern "C" fn wv_destroy_browser(_core: *mut WebViewCore, id: c_int) {
    let mut state = core_lock();
    if let Some(state) = state.as_mut() {
        if let Some(bs) = state.browsers.get_mut(&id) {
            if !bs.closing {
                bs.closing = true;
                state.pending_close += 1;
                if let Some(mut host) = bs.browser.host() {
                    host.close_browser(1);
                }
            }
        }
    }
}

/// 消息泵：每帧调用（CEF 回调在此触发，含 OnPaint → WvOnPaint）。
/// external_begin_frame 模式下需在 pump 后显式 send_external_begin_frame 驱动产帧。
#[unsafe(no_mangle)]
pub extern "C" fn wv_pump(_core: *mut WebViewCore) {
    let (initialized, ids): (bool, Vec<i32>) = {
        let state = core_lock();
        let s = match state.as_ref() {
            Some(s) if s.cef_initialized && !s.cef_failed => s,
            _ => return, // 未初始化/初始化失败：CEF 禁止消息泵工作
        };
        (true, s.browsers.keys().copied().collect())
    };
    if !initialized {
        return;
    }

    cef::do_message_loop_work();

    let state = core_lock();
    if let Some(state) = state.as_ref() {
        for id in &ids {
            if let Some(bs) = state.browsers.get(id) {
                if let Some(mut host) = bs.browser.host() {
                    host.send_external_begin_frame();
                }
            }
        }
    }
}

/// 关闭 CEF（进程级）。
/// 先关闭所有浏览器并**有界泵等待**异步关闭完成（OnBeforeClose 递减 pending_close），
/// 再 CefShutdown——带活浏览器 shutdown 会断言/崩溃。
#[unsafe(no_mangle)]
pub extern "C" fn wv_destroy(core: *mut WebViewCore) {
    if core.is_null() {
        return;
    }
    let was_initialized = {
        let mut state = core_lock();
        let init = state.as_ref().is_some_and(|s| s.cef_initialized);
        if let Some(s) = state.as_mut() {
            if s.cef_initialized && !s.cef_failed {
                // 关闭全部浏览器（尚未关闭的；closing 防重复计数）。
                for bs in s.browsers.values_mut() {
                    if !bs.closing {
                        bs.closing = true;
                        s.pending_close += 1;
                        if let Some(mut host) = bs.browser.host() {
                            host.close_browser(1);
                        }
                    }
                }
            }
        }
        init
    };

    if was_initialized {
        // 有界泵等待：让异步关闭送达 OnBeforeClose（最多 ~120 帧）。
        for _ in 0..120 {
            {
                let state = core_lock();
                if state.as_ref().is_none_or(|s| s.pending_close == 0) {
                    break;
                }
            }
            cef::do_message_loop_work();
        }
    }

    {
        let mut state = core_lock();
        *state = None;
    }
    if was_initialized {
        cef::shutdown();
    }
    unsafe { drop(Box::from_raw(core)) };
    log_stderr("[webview-core] wv_destroy: CEF shutdown\n");
}
