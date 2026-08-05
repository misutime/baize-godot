# Godot 编辑器 UI 重构（TS 路线）——Route B 分发边界说明：打包的游戏不带 godot-cef

> **定位**：本文是《引擎级WebDock-RouteB-方案.md》的补充说明（2026-08-02 编写），回答一个高频疑问：**"这样集成的 godot-cef，在打包好的游戏里也带吗？"** —— **不带**。godot-cef 只随 fork 编辑器分发，游戏导出产物（export template 构建）里没有它。
>
> **证据标注**：仓库事实标注来源（文件 + 行号）；推断标 `[INFERENCE]`。

---

## 1. 一句话结论

**godot-cef 只存在于"分发给游戏开发者的 fork 编辑器"里；导出/打包好的游戏里没有 godot-cef，也不包含任何加载它的代码。**

三个独立机制共同保证这一点：

1. webview 模块**只在编辑器构建中编译**（`config.py` 的 `can_build` 门控）→ 导出模板里没有模块代码；
2. godot-cef 的加载路径是**编辑器 exe 的相对目录** → 游戏 exe 旁边没有这个目录；
3. 分发暂存目标是**编辑器构建产物目录**（`bin/webview/`）→ 它不是项目资源，不会被导出器打包。

## 2. 三种构建形态下 godot-cef 的存在性

| 构建形态 | SCons 参数 | webview 模块 | godot-cef | 说明 |
|---|---|---|---|---|
| 编辑器 dev/release（fork 分发） | `editor=yes` | ✅ 编译 | ✅ 经 `bin/webview/` 自动加载 | WebDock 在此形态下工作 |
| 导出模板（runtime） | `editor=no`（导出模板/游戏用） | ❌ 不编译 | ❌ 不存在 | 游戏本体不含 CEF |
| 项目内 addon（游戏内嵌场景） | 任意 | 与模块无关 | ✅ 经 `res://addons/` 随项目导出 | **另一条用法**，见 §4 |

## 3. 三条证据（源码）

### 证据 1：模块只进编辑器构建 — `modules/webview/config.py:1-2`

```python
def can_build(env, platform):
    return env.editor_build
```

`env.editor_build` 仅在 `editor=yes` 构建（编辑器）时为真；导出模板是 `editor=no` 的 runtime 构建 → **整个模块（含加载逻辑）不进导出模板**。

### 证据 2：加载路径 = 编辑器 exe 旁目录 — `modules/webview/webview_manager.cpp:54-57`

```cpp
// 分发目录约定：<exe_dir>/webview/（开发态 = bin/webview/，由 `just webview-stage` 暂存）。
const String ext_path = OS::get_singleton()->get_executable_path().get_base_dir()
                                .path_join("webview")
                                .path_join("godot_cef.gdextension");
```

从 `get_executable_path()`（编辑器 exe 所在目录）解析扩展位置。游戏导出后生成的 exe 旁边没有 `webview/` 目录——它不是 `res://` 项目内容，导出器不打包它。

### 证据 3：暂存目标 = 编辑器构建产物目录 — `misc/scripts/stage_webview.py:22-24`

```python
ADDON_SOURCE = REPO_ROOT.parent / "refers" / "godot-cef" / "addons" / "godot_cef"
UI_SOURCE = REPO_ROOT.parent / "refers" / "cef-smoke-test" / "ui"
DEST = REPO_ROOT / "bin" / "webview"
```

脚本头注释明示意图："godot-cef 与 baize-godot 独立开发：本脚本只消费 godot-cef 的**编译产物**……由引擎模块在编辑器启动时自动加载（**与打开的项目无关**）"（`stage_webview.py:1-7`）。

## 4. 两种"打包"必须分清

| | 打包游戏 | 分发 fork 编辑器 |
|---|---|---|
| 产物 | 导出模板 + 项目资源（玩家拿到的东西） | `bin/godot.windows.editor.x86_64.exe` + `bin/webview/` |
| 内容 | 游戏逻辑、场景、资源；**无 CEF** | 编辑器 + gdcef 扩展 + CEF 运行时（~100MB）+ `ui/` 页面 |
| 带 godot-cef？ | ❌ | ✅ |
| 这是本计划的意图 | 无 | **是**：web dock 随 fork 编辑器分发，不依赖项目安装插件（Route B 方案 §1） |

对玩家/游戏体积的实际影响：**游戏导出不受 100MB+ CEF 影响**；CEF 只躺在开发者的编辑器安装目录里。

## 5. 边界情况：项目自装 addon 的用法（会带）

如果某个**游戏项目自己**把 godot-cef 当普通 GDExtension 装进 `res://addons/`，那么导出游戏时**会**带上它——因为 GDExtension 是项目级资源，扩展清单 `.godot/extension_list.cfg` 随项目数据走，导出模板本身支持加载扩展。

这正是《方案.md》§10 保留的口径："游戏内嵌/工具场景继续纯 GDExtension"——它与 Route B 的编辑器 WebDock 是**两条独立用法**：

| 用法 | 加载方式 | 分发物 | 游戏导出带不带 |
|---|---|---|---|
| **Route B 编辑器 WebDock**（本计划） | 模块在 exe 旁 `webview/` 目录自动加载 | 编辑器 | ❌ 不带 |
| **游戏内嵌 CEF**（addon 用法） | 项目 `res://addons/` + GDExtensionManager | 项目 | ✅ 带 |

判断口诀：**CEF 在 `<exe>/webview/` = 编辑器专属；CEF 在 `res://addons/` = 跟着项目走（导出会带）。**

## 6. 结论

- 按 Route B 计划集成的 godot-cef，**打包好的游戏里没有**，也不会有加载它的代码路径。
- 需要 CEF 的游戏场景请走 §5 的 addon 用法，与本计划互不影响。
- 若未来形态变化（如 §7 演进到 Rust staticlib 融合、或改为项目级分发），本文需同步修订；当前形态下结论成立。

## 引用

- `modules/webview/config.py:1-2`（can_build 门控）
- `modules/webview/webview_manager.cpp:54-66`（exe 旁目录加载 + 未暂存可观测提示）
- `misc/scripts/stage_webview.py:1-7`（脚本意图注释）、`:22-24`（来源/目标路径）
- 《引擎级WebDock-RouteB-方案.md》§1（MVP 目标）、§8（构建与分发）、§10（口径修订）
