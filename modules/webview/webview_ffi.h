/**************************************************************************/
/*  webview_ffi.h                                                         */
/**************************************************************************/
/* C ABI 契约（4A）：C++ 壳（modules/webview）与 Rust 核心                */
/* （crates/webview-core）的唯一通信面。                                  */
/* 边界纪律：只承载浏览器语义（lifecycle/paint/message/input/ime），       */
/* 禁止 Godot 对象模型（Variant/Object/ClassDB）穿越——防止退化为          */
/* mini-gdext。                                                           */
/**************************************************************************/

#ifndef WEBVIEW_FFI_H
#define WEBVIEW_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct WebViewCore WebViewCore;

typedef void (*WvOnPaint)(void *userdata, int32_t id, const uint8_t *rgba, uint32_t width, uint32_t height);
typedef void (*WvOnMessage)(void *userdata, int32_t id, const char *json);
typedef void (*WvOnLoadStatus)(void *userdata, int32_t id, int32_t status, const char *url);

typedef struct WvCallbacks {
	WvOnPaint on_paint;
	WvOnMessage on_message;
	WvOnLoadStatus on_load_status;
} WvCallbacks;

/* 生命周期 */
WebViewCore *wv_create(const char *exe_dir, const WvCallbacks *callbacks, void *userdata);
void wv_destroy(WebViewCore *core);
void wv_pump(WebViewCore *core); /* 消息泵：C++ 壳每帧调用 */

/* 浏览器（OSR 软件渲染；paint 经 WvOnPaint 回调返回 RGBA 缓冲，仅回调期间有效，需拷贝） */
int32_t wv_create_browser(WebViewCore *core, int32_t id, const char *url, uint32_t width, uint32_t height);
int32_t wv_resize_browser(WebViewCore *core, int32_t id, uint32_t width, uint32_t height);
int32_t wv_navigate_browser(WebViewCore *core, int32_t id, const char *url);
void wv_destroy_browser(WebViewCore *core, int32_t id);

#ifdef __cplusplus
}
#endif

#endif /* WEBVIEW_FFI_H */
