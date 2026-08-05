# att_webview — CEF WebUI 集成

> 本 fork 特有模块（`att_` 前缀 = 别于上游 Godot）。渲染路线最终态 = **非 OSR 窗口模式**
> （CEF 原生子窗口、像素零回传、输入/IME 原生直收）；OSR 时代方案已归档。

## 职责（一句话）

**把 CEF 浏览器嵌入 Godot 编辑器**：WebDock 面板承载 React 前端（`web/ui`），
`WebBridge` 提供页面 ↔ 编辑器的方法/事件桥（协议见《WebUI架构-桥协议与前端SDK.md》）。

## 文件结构

| 文件 | 角色 |
|---|---|
| `webview_core.{h,cpp}` | CEF 生命周期核心：初始化/浏览器创建/消息回调（main frame 校验，S2 事件面） |
| `webview_manager.{h,cpp}` | 浏览器实例管理：创建/导航/输入转发/帧泵驱动 |
| `web_panel.{h,cpp}` | Godot 控件壳（WebPanel）：子窗口矩形同步/可见性/加载事件 |
| `editor_web_dock.{h,cpp}` | 编辑器 dock 接线 + 事件源（`set_event_browser_id`，S2 多目标化） |
| `web_bridge.{h,cpp}` | 方法/事件桥：10 方法分派 + 事件下行；**能力方法委托 `att_editor_ops` 的 Registry**（4 已委托，其余在途） |
| `cef_application_mac.{h,cpp}` | macOS CEF 消息泵适配（CrAppProtocol 注入） |
| `webview_runtime_path.h` | 运行时根目录解析（.app bundle 内 Contents/Resources） |

## 构建与暂存（重要单点）

- **`CEF_SDK_VERSION` 只在 `SCsub` 定义**（单点，勿多处硬编码）——`misc/scripts/stage_webview.py` 读取；
- 预构建：cmake → `bin/obj/webview/cefviewcore`（SCsub 与 stage 脚本共用路径）；
- 暂存：CEF 运行时 → `bin/webview/`，UI（React 壳）→ `bin/webview/ui/`
  （`editor_web_dock` 用 `file:///<exe_dir>/webview/ui/index.html` 加载）；
- 平台门控：Windows x86_64 MSVC + macOS arm64/x86_64（SCsub 显式报错其余平台）。

## 边界与已知约束

- 渲染：非 OSR 窗口模式（`windowless_rendering_enabled=0`，`SetAsChild`）；
- WebPanel 仅支持宿主主窗口根视口（嵌入视口/CanvasLayer 层级显式告警）；
- 事件源目前**单浏览器目标**（`set_event_browser_id`），S2 重构为多目标 fan-out；
- CEF cache 按实例槽位隔离（`webview_core.cpp`），支持多编辑器实例。

## 相关文档

- 《WebUI架构-桥协议与前端SDK.md》（桥协议语义：`{ok,result}`、req_id、点号命名空间）
- 《页面渲染选型-OSR与非OSR/》（渲染路线选型与落地记录，历史归档）
- 《WebUI前端工程-实现文档-sdk与ui-workspace.md》（`web/` 前端工程）
