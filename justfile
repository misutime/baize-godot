# 引擎级 WebDock（4A）开发/测试命令
# 用法：just [recipe]，just 不带参数 = 列出配方
# 分发模型：CEF 运行时 + helper + 页面产物随编辑器分发（bin/ + bin/webview/ui/），与打开的项目无关

set shell := ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# 常用路径
exe     := "bin" / "godot.windows.editor.dev.x86_64.console.exe"
project := "D:\\misutime\\104_game\\refers\\cef-b0-test"

# 列出所有配方
default:
    just --list

# 构建 dev 编辑器（等价 task dev，-j20 + debug_symbols=no）
dev:
    task dev

# 暂存 4A 产物（helper + CEF 运行时 + 页面）到编辑器分发目录（统一走 task stage-webview）
webview-stage:
    task stage-webview

# 编辑器冒烟：打开测试项目，WebDock 应渲染 bin/webview/ui/ 页面
b0-load:
    & "{{exe}}" --path {{project}} --editor
