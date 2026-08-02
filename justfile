# 引擎级 WebDock（C++ 路线）开发/测试命令
# 用法：just [recipe]，just 不带参数 = 列出配方
# 分发模型：CefViewCore 源码编入引擎；CEF 运行时 + helper（CefViewWing.exe）+ 页面产物
# 随编辑器分发（bin/ + bin/webview/），与打开的项目无关
#
# 首次构建顺序：
#   1. just webview-stage  —— 预构建 libcef_dll_wrapper.lib + CefViewWing.exe + CEF 运行时
#                             （首次/换 CEF 版本才构建；产物存在则跳过，秒级）
#   2. just dev             —— 编引擎（含 CefViewCore 源码 + 链接 stage 产物）
# 跳过第 1 步直接 dev 会报错并提示先跑 stage-webview（不静默）。

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

# 预构建 CEF 产物并暂存到分发目录（统一走 task stage-webview，单一入口）
# 首次/换 CEF 版本：构建 libcef_dll_wrapper.lib + CefViewWing.exe + CEF 运行时 → bin/ + bin/webview/ui/
# 产物已就绪：跳过构建，仅暂存（秒级）
webview-stage:
    task stage-webview

# 编辑器加载态：自动初始化 CEF 并显示 WebDock
# 预期日志：CEF initialized -> WebPanel browser created -> WebDock registered -> page loaded (200)
dev-run:
    & "{{exe}}" --path {{project}} --editor
