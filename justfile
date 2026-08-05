# 引擎级 WebDock（C++ 路线）开发/测试命令
# 用法：just [recipe]，just 不带参数 = 列出配方
# 分发模型：CefViewCore 源码编入引擎；CEF 运行时 + helper（CefViewWing）+ 页面产物
# 随编辑器分发。双平台：Windows x64 / macOS arm64。mac 上两种启动形态均可用：
#   1. 终端裸可执行文件（bin/godot.macos.editor.dev.arm64，运行时在 bin/ 同级）
#   2. .app bundle（启动台/双击；运行时 + UI 在 bundle 内 Contents/Frameworks +
#      Contents/Resources/webview/ui，CEF mac 标准布局）
#
# 构建（2026-08-03 起自动化）：task dev / just dev 内部已由 build.py 内置 stage-webview
# 前后钩子——构建前确保预构建产物（首次自动下载 SDK），构建后暂存 bin/ 与 mac bundle。
# 无需手动先跑 stage-webview。UI 变更后补 task ui-build（下次构建自动入 bundle）。

set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# 常用路径(双平台:just 的 os() 运行时判定;未支持平台显式报错,不静默当 Windows)
# mac:dev 构建产物 bin/godot.macos.editor.dev.arm64(Apple Silicon);
#     项目路径为仓库相对 ../refers/cef-b0-test(与本仓库 refers/ 布局一致,按实际位置调整)
exe     := if os() == "macos" { "bin" / "godot.macos.editor.dev.arm64" } else if os() == "windows" { "bin" / "godot.windows.editor.dev.x86_64.console.exe" } else { error("unsupported platform for dev-run: " + os()) }
project := if os() == "macos" { "../refers/cef-b0-test" } else if os() == "windows" { "D:\\misutime\\104_game\\refers\\cef-b0-test" } else { error("unsupported platform for dev-run: " + os()) }

# 列出所有配方
default:
    just --list

# 构建 dev 编辑器（等价 task dev，-j20 + debug_symbols=no）
dev:
    task dev

# 预构建 CEF 产物并暂存到分发目录（统一走 task stage-webview，单一入口）
# 首次/换 CEF 版本：构建 libcef_dll_wrapper + CefViewWing + CEF 运行时 → bin/ + bin/webview/ui/
# 产物已就绪：跳过构建，仅暂存（秒级）
webview-stage:
    task stage-webview

# 编辑器加载态：自动初始化 CEF 并显示 WebDock
# 预期日志：CEF initialized -> WebPanel browser created -> WebDock registered -> page loaded (200)
dev-run:
    "{{exe}}" --path {{project}} --editor
