//! 引擎 webview Rust 核心（4A 方案）。
//!
//! 组成：
//! - `cef-app`（独立 crate，vendor 自 godot-cef）：CEF 应用层（OsrApp / 浏览器/渲染进程 handler / V8 绑定）
//! - `software_render`（vendor 自 godot-cef，MIT）：软件渲染合成
//! - `core`：C ABI 导出 + CEF 生命周期/浏览器/软件渲染（M1b）
//!
//! 边界纪律：本 crate 不依赖 Godot，只经 C ABI 与 C++ 壳（modules/webview/）通信。

mod core;
mod software_render;
