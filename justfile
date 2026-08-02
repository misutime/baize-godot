# 引擎级 WebDock（Route B）开发/测试命令
# 用法：just [recipe]，just 不带参数 = 列出配方
# 注：B0 验证命令针对 cef-b0-test 项目；bin 相对本文件（baize-godot 根目录）

set shell := ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# 常用路径
exe       := "bin" / "godot.windows.editor.dev.x86_64.console.exe"
project   := "D:\\misutime\\104_game\\refers\\cef-b0-test"
cef_ext   := "D:\\misutime\\104_game\\refers\\godot-cef\\addons\\godot_cef\\godot_cef.gdextension"
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

# B0 惰性态：不设 GODOT_CEF_EXTENSION，模块应打印 skip 行且编辑器正常打开
b0-inert:
    & "{{exe}}" --path {{project}} --editor

# B0 加载态：设置 GODOT_CEF_EXTENSION 后打开编辑器
# 预期日志：Loading CEF extension -> Initialize godot-rust -> loaded OK
b0-load:
    $env:GODOT_CEF_EXTENSION = "{{cef_ext}}"; & "{{exe}}" --path {{project}} --editor

# B0 无头类检查：确认 CefTexture 已注册（加载态下运行）
b0-check:
    $env:GODOT_CEF_EXTENSION = "{{cef_ext}}"; & "{{exe}}" --headless --path {{project}} --script res://check.gd
