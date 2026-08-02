# 引擎级 WebDock（Route B）开发/测试命令
# 用法：just [recipe]，just 不带参数 = 列出配方
# 分发模型：gdcef 扩展 + 页面产物随编辑器分发（bin/webview/），与打开的项目无关

set shell := ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# 常用路径
exe       := "bin" / "godot.windows.editor.dev.x86_64.console.exe"
project   := "D:\\misutime\\104_game\\refers\\cef-b0-test"
gdcef_dir := "D:\\misutime\\104_game\\refers\\godot-cef"

# 列出所有配方
default:
    just --list

# 构建 dev 编辑器（等价 task dev，-j20 + debug_symbols=no）
dev:
    task dev

# 构建 gdcef 扩展（Rust，绕过 mise；需要 nightly + CEF_PATH）
gdcef-build:
    cd {{gdcef_dir}}; $env:CEF_PATH = "$env:USERPROFILE\.local\share\cef"; $env:PATH = "$env:PATH;$env:CEF_PATH"; cargo +nightly-2026-07-28 xtask bundle --release

# 暂存 gdcef 产物到编辑器分发目录 bin/webview/（统一走 task stage-webview，单一入口）
webview-stage:
    task stage-webview

# 未暂存态：预期打印 "[WebView] CEF extension not staged ... run just webview-stage"
b0-inert:
    & "{{exe}}" --path {{project}} --editor

# 加载态：编辑器自动从 bin/webview/ 加载扩展并显示 WebDock
# 预期日志：Loading CEF extension -> Initialize godot-rust -> loaded OK -> WebDock registered
b0-load:
    & "{{exe}}" --path {{project}} --editor

# 无头类检查：确认 CefTexture 已注册（需先 webview-stage）
b0-check:
    & "{{exe}}" --headless --path {{project}} --script res://check.gd
