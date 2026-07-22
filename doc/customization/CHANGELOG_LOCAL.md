# Baize Godot 本地修改日志

> 本文件记录本仓库相对上游 [godotengine/godot](https://github.com/godotengine/godot) 的所有本地修改。
> 基线：上游 commit `9a33066a27`（Godot 4.7 beta 期间）。当前 HEAD：`e6d007d713`。
> 生成日期：2026-07-22。以实际代码 diff 为准，git message 仅作参考。

---

## 目录

- [1. 品牌与版本标识](#1-品牌与版本标识)
- [2. Inspector 重构](#2-inspector-重构)
- [3. 3D 编辑器吸附系统重做](#3-3d-编辑器吸附系统重做)
- [4. 3D 视口导航与自由视角](#4-3d-视口导航与自由视角)
- [5. 主屏场景标签显示优化](#5-主屏场景标签显示优化)
- [6. SpinBox show_buttons 属性](#6-spinbox-show_buttons-属性)
- [7. Node3D 缩放回退浮点修复](#7-node3d-缩放回退浮点修复)
- [8. Windows 进程路径解析增强](#8-windows-进程路径解析增强)
- [9. C# / .NET 工具链改造](#9-c--net-工具链改造)
- [10. NuGet Baize 发布流程](#10-nuget-baize-发布流程)
- [11. 构建自动化](#11-构建自动化)
- [12. 自定义文档体系](#12-自定义文档体系)
- [13. Gizmo 虚方法暴露](#13-gizmo-虚方法暴露)
- [14. 杂项](#14-杂项)
- [15. 实验性远程分支（未合入 master）](#15-实验性远程分支未合入-master)

---

## 1. 品牌与版本标识

### version.py
- `status` 字段从 `"beta"` 改为 `"baize1"`，引擎报告版本为 `4.7.0-baize1`。
- 发布脚本会解析此值生成 NuGet 语义版本 `4.7.0-baize.1`。

### Windows 图标替换
- `platform/windows/godot.ico` — 替换为 Baize 自定义图标。
- `platform/windows/godot.png` — 新增 PNG 版本（上游原无此文件）。
- `platform/windows/godot_console.ico` — 控制台图标同步替换。

---

## 2. Inspector 重构

**涉及文件：** `editor/inspector/editor_inspector.cpp`、`editor_inspector.h`、`editor_properties.cpp`、`editor_properties_vector.cpp`、`editor_properties_vector.h`、`editor/settings/editor_settings.cpp`、`editor/themes/theme_classic.cpp`、`editor/themes/theme_modern.cpp`、`scene/3d/node_3d.cpp`

这是本次 fork 中改动量最大的编辑器功能区域，包含以下子特性：

### 2.1 分类级折叠（Category Folding）
- `EditorInspectorCategory` 新增成员：`folding_key`、`object`、`content_vbox`、`force_unfolded`、`header_hover`。
- 新增方法：`setup_folding()`、`update_content_visibility()`、`unfold()`、`fold()`、`_get_arrow()`、`_is_unfolded()`。
- 分类节点拥有自己的 `VBoxContainer` 子容器；创建时默认隐藏，属性解析进此容器。
- 分类按继承顺序重排：Node → Node3D → 具体类型。
- 分类标题栏绘制：左侧 Tree 箭头图标指示折叠状态，悬停时透明度变化，左键/`ui_accept` 切换折叠。
- `collapse_all_folding()`、`expand_all_folding()`、`expand_revertable()` 也会遍历分类列表。

### 2.2 全局折叠状态持久化
- 静态成员 `session_folded_inspector_paths`（`HashSet<String>`）。
- 通过 `EditorSettings::get_project_metadata()` / `set_project_metadata()` 持久化到 `"inspector"` section / `"global_folded_paths"` key。
- 默认折叠路径：`"__category_folded/Node"`。
- `EditorInspectorSection::_is_unfolded()` 在 `inspector_path` 非空时查询全局折叠状态（而非 per-object 状态）。

### 2.3 行悬停效果（Row Hover）
- `EditorProperty` 新增 `row_hover` 布尔值。
- `NOTIFICATION_MOUSE_ENTER` / `NOTIFICATION_MOUSE_EXIT` 控制状态。
- 绘制时在悬停行覆盖一个极淡的着色矩形（`property_color` alpha 0.035）。
- 移除了选中状态切换背景样式的逻辑。

### 2.4 网格线（Grid Lines）
- 属性行底部绘制水平分隔线（alpha 0.07），仅当下方有可见兄弟属性时。
- 名称列与值列之间绘制垂直分隔线，位于 `right_child_rect` x 位置。
- 网格线使用 `full_size` 绘制，跨越完整宽度（不受缩进影响）。

### 2.5 内联 Node3D 变换向量
- `EditorPropertyVectorN` 和 `EditorPropertyVector3` 构造函数新增 `bool p_inline = false` 参数。
- `p_inline = true` 时：强制水平布局，各分量 SpinSlider 和链接按钮最小尺寸 `(0, 44) * EDSCALE`。
- Node3D 的 position/rotation/scale 属性以 inline 模式渲染，`name_split_ratio` 设为 0.25。

### 2.6 节区（Section）布局重构
- 标题栏高度改用 `theme_cache.font`（非 bold_font），增加 `padding_size * 2` 内边距，最小 34*EDSCALE。
- 标题栏背景 alpha 从 0.4 降至 0.12，悬停亮度减弱。
- 箭头绘制在标题缩进左侧，标题起始于 `section_header_indent + 12*EDSCALE`。
- 子控件占满 `get_size()` 宽度（不再被 `inspector_margin` 减去），缩进仅应用于文本/箭头绘制坐标。
- `get_minimum_size()` 改用 `_get_header_height()` 代替原始字体高度。

### 2.7 脚本属性独立面板
- `EditorInspector` 新增 `script_section_vbox`（`VBoxContainer`）。
- 遇到 `script` 属性时，创建 `EditorInspectorScriptPanel` 主题变体的 `PanelContainer`，脚本属性渲染在此独立面板中，与常规属性视觉分离。

### 2.8 分类样式：直角标题栏 + 底部分界线
- **theme_classic.cpp**：分类 `StyleBoxFlat` 强制 `corner_radius_all(0)`，添加 `SIDE_BOTTOM` 边框（`dark_color_2`）。
- **theme_modern.cpp**：同上，边框颜色使用 `extra_border_color_2`。
- 中文注释："一级分类使用直角标题栏和底部分界线，和属性表格形成清晰层级。"

### 2.9 其他
- 新增 `inspector_margin` 主题常量（`ThemeCache`），来自 Editor 主题。
- `create_default_inspector()` 默认隐藏元数据（`set_hide_metadata(true)`）。
- 移除了冗余的 `AccessibilityServer::update_set_value()` 调用（仅保留 `update_set_name()`）。
- `get_minimum_size()` 现在也考虑标签宽度、还原图标宽度和固定图标宽度。

---

## 3. 3D 编辑器吸附系统重做

**涉及文件：** `editor/scene/3d/node_3d_editor_plugin.cpp`、`node_3d_editor_plugin.h`，以及 12 个 Gizmo 插件文件

### 3.1 单一吸附开关 → 三个独立开关
- **上游**：单一 `snap_enabled` 布尔值 + 一个 `is_snap_enabled()` 方法。
- **本地**：分解为 `snap_translate_enabled`、`snap_rotate_enabled`、`snap_scale_enabled` 三个独立布尔值（默认均为 `true`）。
- 对应三个 getter：`is_translate_snap_enabled()`、`is_rotate_snap_enabled()`、`is_scale_snap_enabled()`，各自与其类型的 `snap_key_enabled` 做 XOR。
- `is_snap_enabled()` 改为返回三者的 OR。

### 3.2 吸附对话框 → 工具栏内联 SpinBox
- **删除**：`snap_dialog`（`ConfirmationDialog`）及 `MENU_TRANSFORM_CONFIGURE_SNAP` 菜单项。
- **新增**：三个内联 `SpinBox` 控件直接嵌入工具栏：
  - `snap_translate`：min=0.1, step=0.1, max=10, suffix='m', 最小宽度 60px, 无按钮。
  - `snap_rotate`：min=1.0, step=1.0, max=360, suffix='°', 最小宽度 52px。
  - `snap_scale`：min=10.0, step=10.0, max=100, suffix='%', 最小宽度 52px。
- 三个切换按钮图标分别改为 `ToolMove`、`ToolRotate`、`ToolScale`（上游统一用 `Snap`）。

### 3.3 吸附最小值强制
- 定义常量：`MIN_TRANSLATE_SNAP = 0.1`、`MIN_ROTATE_SNAP = 1.0`、`MIN_SCALE_SNAP = 10.0`。
- 所有吸附值读写处都用 `MAX(value, MIN_*_SNAP)` 钳位。

### 3.4 缩放吸附改进
- 新增静态辅助函数 `_snap_basis_scale()`：按轴基于原始缩放和吸附百分比对 Basis 缩放做步进吸附。
- `_compute_transform()` 的 `TRANSFORM_SCALE` 分支增加二次 pass 调用 `_snap_basis_scale()`。
- 支持本地坐标和全局坐标模式（全局模式下先转本地、吸附、再转回）。

### 3.5 所有 Gizmo 插件更新
以下 12 个文件中的 `is_snap_enabled()` 全部替换为类型对应检查（通常为 `is_translate_snap_enabled()`）：

| 文件 | 变更 |
|------|------|
| `camera_3d_gizmo_plugin.cpp` | FOV/size handle |
| `gizmo_3d_helper.cpp` | box/cylinder/capsule/cone handle |
| `gpu_particles_collision_3d_gizmo_plugin.cpp` | radius handle |
| `light_3d_gizmo_plugin.cpp` | spot range + omni/spot radius handle |
| `occluder_instance_3d_gizmo_plugin.cpp` | 5 处 handle（Sphere/Quad/BoxOccluder3D） |
| `collision_shape_3d_gizmo_plugin.cpp` | cylinder height + separation ray length |
| `reflection_probe_gizmo_plugin.cpp` | size handle |
| `visible_on_screen_notifier_3d_gizmo_plugin.cpp` | primary + secondary axis handle |
| `path_3d_editor_plugin.cpp` | point position（translate）+ tilt（rotate） |
| `polygon_3d_editor_plugin.cpp` | point placement |

### 3.6 吸附状态序列化
- `get_state()` / `set_state()` 使用新的 per-type key。
- 旧 `snap_enabled` key 仍可读取（向后兼容，设置三个开关为相同值）。
- `_snap_value_changed()` 通过 project metadata 持久化值。
- `_snap_update()` 使用 `set_value_no_signal()` 避免反馈循环。

### 3.7 工具选项按钮拆分
- `TOOL_OPT_USE_SNAP` 拆分为 `TOOL_OPT_USE_TRANSLATE_SNAP`、`TOOL_OPT_USE_ROTATE_SNAP`、`TOOL_OPT_USE_SCALE_SNAP`。
- 对应菜单项 `MENU_TOOL_USE_TRANSLATE_SNAP` 等。

---

## 4. 3D 视口导航与自由视角

**涉及文件：** `editor/scene/3d/node_3d_editor_plugin.cpp`、`node_3d_editor_plugin.h`、`editor/settings/editor_settings.cpp`、`editor/settings/editor_settings_dialog.cpp`

### 4.1 Godot 导航预设修改
在 `update_navigation_preset()` 中，`NAV_SCHEME_GODOT` 预设的两项默认值被修改：

| 设置 | 上游值 | 本地值 | 效果 |
|------|--------|--------|------|
| `set_orbit_mouse_button` | `NAV_MOUSE_BUTTON_MIDDLE` | `NAV_MOUSE_BUTTON_RIGHT` | 轨道旋转改为右键 |
| `pan_mod_key_1` | `Key::SHIFT` | `Key::NONE` | 平移无需修饰键（中键直接平移） |

### 4.2 视口平移修饰键
- `viewport_pan_modifier_1` 默认值从 `Key::SHIFT` 改为 `Key::NONE`。

### 4.3 自由视角（Freelook）激活方式
- **上游**：右键 + 修饰键激活，松开右键退出。
- **本地**：鼠标侧键 4（`MB_XBUTTON1`）切换激活/退出（toggle 行为）。
- 原右键 handler 中的 14 行 freelook 代码被移除。

### 4.4 自由视角 UI 反馈
- 新增 `Label *freelook_label`，放置在 `bottom_center_vbox`，居中，默认隐藏。
- 进入自由视角时显示中文提示：`"飞行视角：WASD 移动，侧键 4 或 Shift+F 退出"`。
- 退出时显示：`"退出飞行视角"`。

### 4.5 轨迹球旋转修正
- `apply_transform()` 中，local_coords 条件现在也排除 `_edit.is_trackball`——轨迹球旋转始终使用全局坐标。

### 4.6 3D 视口默认裁剪平面
- `editor/settings/editor_settings.cpp` 中调整了 3D 视口的默认近远裁剪平面设置（细微差异，约 1 行变更）。

---

## 5. 主屏场景标签显示优化

**涉及文件：** `editor/editor_main_screen.cpp`、`editor/editor_node.cpp`、`editor/editor_node.h`

- `EditorNode` 新增 `update_scene_tabs_visibility()` 方法。
- 场景标签仅在 2D 或 3D 编辑器激活时显示；切换到脚本编辑器、游戏视图或资源商店时隐藏。
- `EditorMainScreen::select()` 中新增调用 `update_scene_tabs_visibility()`。
- 中文注释："场景标签只对应 2D/3D 场景视图；脚本、游戏和资源商店里显示它会让人误以为标签能切回视图。"

---

## 6. SpinBox show_buttons 属性

**涉及文件：** `scene/gui/spin_box.cpp`、`scene/gui/spin_box.h`

新增 `show_buttons` 布尔属性（默认 `true`），允许隐藏 SpinBox 的上下箭头按钮。

当 `show_buttons = false` 时：
- 按钮区域宽度折叠为 0。
- 鼠标命中检测跳过。
- 拖拽被禁用（`drag.allowed = false`）。
- 按钮渲染跳过。
- 定时器停止。
- 已有的 hover/pressed 状态被清除。

注册为 GDScript 属性：`set_show_buttons()` / `is_showing_buttons()`。

---

## 7. Node3D 缩放回退浮点修复

**涉及文件：** `scene/3d/node_3d.cpp`

- `_property_get_revert()` 中 scale 分支增加 `is_equal_approx` 检查。
- 当 `current_scale` 与矩阵分解得到的 `revert_scale` 近似相等时，直接返回 `current_scale`。
- 防止矩阵分解的极小浮点误差导致 Inspector 重置按钮误亮。
- 中文注释："Inspector 的重置按钮不要因为矩阵分解后的极小误差亮起"。

---

## 8. Windows 进程路径解析增强

**涉及文件：** `platform/windows/os_windows.cpp`

`create_process()` 增强了 Windows 环境下的外部编辑器路径解析能力：

### 8.1 新增辅助函数
- `_windows_process_path_has_directory()`：检测路径是否包含 `/` 或 `\` 分隔符。
- `_windows_search_process_path()`：使用 `SearchPathW` API 解析裸命令名（如 `code`）为完整路径。
  - 尊重 Windows `PATH` 和 `PATHEXT` 环境变量。
  - 无扩展名时按 PATHEXT 顺序尝试 `.COM` → `.EXE` → `.BAT` → `.CMD`。
  - 优先匹配 `.cmd` 而非无扩展名 shim（解决 VS Code 的 `code` → `code.cmd` 问题）。

### 8.2 执行行为
- 非绝对路径调用 `_windows_search_process_path()` 解析。
- 解析结果以 `.cmd` 或 `.bat` 结尾时，通过 `ComSpec`（`cmd.exe /C`）包装执行。
- 绝对路径仍使用原有 `fix_path()` 逻辑。

---

## 9. C# / .NET 工具链改造

**涉及文件：** `modules/mono/` 下 10 个文件

### 9.1 NuGet 包 Baize 品牌化
所有 NuGet 包 ID 添加 `Baize.` 前缀：

| 上游 PackageId | Baize PackageId |
|----------------|-----------------|
| `Godot.NET.Sdk` | `Baize.Godot.NET.Sdk` |
| `Godot.SourceGenerators` | `Baize.Godot.SourceGenerators` |
| `GodotSharp` | `Baize.GodotSharp` |
| `GodotSharpEditor` | `Baize.GodotSharpEditor` |

`Sdk.targets` 中三个隐式 `PackageReference` 同步重命名。

### 9.2 目标框架升级
- `GodotMinimumRequiredTfm` 从 `net8.0` 改为 `net10.0`。
- 移除了 Android 平台 `net9.0` 条件覆盖。

### 9.3 .slnx 解决方案格式支持
- `DotNetSolution.cs` 新增 `SaveSlnx()` 方法，使用 `Microsoft.VisualStudio.SolutionPersistence` 生成 XML 格式的 `.slnx`。
- 原 `Save()` 改为调用 `SaveSlnx()`；旧 `.sln` 逻辑移至 `SaveLegacySln()`。
- `GodotTools.ProjectEditor.csproj` 新增 NuGet 依赖：`Microsoft.VisualStudio.SolutionPersistence` 1.0.52。
- `GodotSharpDirs.cs`：`.slnx` 搜索优先于 `.sln`；默认回退路径从 `.sln` 改为 `.slnx`。

### 9.4 C# 编辑器自动创建项目
- `GodotSharpEditor.cs` 中 `CreateProjectSolutionIfNeeded()` 条件从 `||` 改为 `&&`（仅当 `.sln` 和 `.csproj` 都不存在时才创建）。
- 在 `_EnablePlugin()` 中调用 `CreateProjectSolutionIfNeeded()`，首次打开空项目时自动生成项目文件。

### 9.5 NuGet.config 保存
- `ProjectGenerator.cs` 新增 `SaveNuGetConfig()` 方法，写入项目级 `NuGet.config`，指向本地 nupkgs 目录 + nuget.org。

### 9.6 已添加后完全回退的变更（净变更 = 0）
- `bindings_generator.cpp` / `.h`：曾添加中文 PO 翻译支持（`_load_csharp_doc_translations` 等），后完全回退。
- `script_create_dialog.cpp`：曾将默认脚本语言改为 C#，后回退为 GDScript。

---

## 10. NuGet Baize 发布流程

**新增文件：**
- `misc/customization/push-nuget-packages.py`（297 行）
- `doc/customization/nuget-publish-manifest.json`
- `doc/customization/nuget-publish.md`

### 发布脚本功能
- 从 `.env` 加载 API key（已 gitignore）。
- 收集 `Baize.` 前缀的 `.nupkg` 文件。
- 计算文件 SHA256 和 payload SHA256（忽略 NuGet 签名和 ZIP 元数据）。
- 与 manifest 对比：同版本+同内容=跳过；同版本+不同内容=报错需升版；新版本=上传。
- 执行 `dotnet nuget push` 后更新 manifest JSON。
- 支持 `--dry-run`、`--skip-duplicate` 等选项。

### 已发布版本
- 4 个包均已在 2026-06-05 发布版本 `4.7.0-baize.1`。

---

## 11. 构建自动化

**新增文件：** `justfile`、`misc/customization/` 下 9 个脚本/配置

### justfile（任务运行器）
| 命令 | 功能 |
|------|------|
| `just mac-csharp [preset] [jobs]` | 构建 macOS C# 编辑器 |
| `just mac-csharp-pro [jobs]` | 构建 macOS C# pro 编辑器 |
| `just win-csharp [preset] [jobs]` | 构建 Windows C# 编辑器（PowerShell） |
| `just win-csharp-pro [jobs]` | 构建 Windows C# pro 编辑器 |
| `just nuget-dry-run` | 预览 NuGet 包上传 |
| `just nuget-push` | 上传 NuGet 包 |

### 构建脚本
| 脚本 | 说明 |
|------|------|
| `build-windows.ps1` | Windows 构建，支持 dev/pro/dev-no-d3d12/prune-vr-xr/soft-prune 预设 |
| `build-windows-csharp.ps1` | Windows C# 三步构建（mono editor → glue → assemblies） |
| `build-macos.sh` | macOS 构建，支持 dev/pro 预设 |
| `build-macos-csharp.sh` | macOS C# 五步构建（含 .app bundle 重新生成） |

### SCons 配置文件（`misc/customization/scons-profiles/`）

| 文件 | 平台 | 用途 |
|------|------|------|
| `windows_3d_dev.py` | Windows | 日常开发基线（dev_build=yes, accesskit=no, angle=no） |
| `windows_3d_pro.py` | Windows | 日常使用/体验测试（release 性能） |
| `windows_3d_dev_no_d3d12.py` | Windows | D3D12 故障排除回退 |
| `windows_3d_prune_vr_xr.py` | Windows | VR/XR 裁剪验证 |
| `windows_3d_soft_prune_experimental.py` | Windows | 实验性软裁剪 |
| `macos_3d_dev.py` | macOS | 日常开发（Metal, vulkan=no） |
| `macos_3d_pro.py` | macOS | 日常使用（Metal, release） |

---

## 12. 自定义文档体系

**新增目录：** `doc/customization/`（11 个文件，约 1,370 行）

| 文件 | 内容 |
|------|------|
| `README.md` | 入口文档。项目定位：面向初学者/独立开发者的中小型风格化 3D 游戏定制引擎。当前阶段：Phase 0（建立规则，不大规模删除源码）。 |
| `customization-rules.md` | 裁剪总则。4 级特性分级（A: 核心 3D / B: 保留但裁剪 / C: 裁剪候选 / D: 禁止触碰）。6 条裁剪原则（隐藏→禁用→删除顺序、保持编辑器可用性、补丁可同步、删除可追溯等）。4 阶段实施计划。 |
| `removal-ledger.md` | 功能禁用追踪表。定义状态（候选/软禁用/源码删除/保留/回滚）。基线记录：D3D12（已回滚恢复）、AccessKit（软禁用）、ANGLE（软禁用）。候选：2D 物理/导航（暂停）、VR/XR（实验中）、2D 编辑器工作区（候选）。 |
| `upstream-sync-policy.md` | 上游合并策略。优先合入：3D 渲染/材质/着色器/灯光、3D 导入、3D 物理/导航、编辑器稳定性、安全修复。默认跳过：纯 2D、C#/.NET、VR/XR、AAA 专用功能。 |
| `build-profiles.md` | 构建配置参考（252 行）。覆盖 Windows/macOS 所有预设、C# 完整构建流程、SCons 参数说明、输出路径、版本验证命令。 |
| `getting-started-windows.md` | Windows 快速上手（134 行）。工具准备、D3D12 依赖安装、各预设构建命令、编辑器启动方式。 |
| `getting-started-macos.md` | macOS 快速上手（156 行）。Xcode/Python/SCons 准备、dev/pro/C# 构建、.app 启动方式。 |
| `godot-default-minus-z-forward-guide.md` | Godot 默认 `-Z` 前方坐标系直觉指南。解释官方约定（+X 右、+Y 上、-Z 前），Front View 语义，MODEL_FRONT 例外。 |
| `nuget-publish.md` | NuGet 发布指南（121 行）。版本派生、发布步骤、manifest 机制、本地 NuGet 源选项。 |
| `nuget-publish-manifest.json` | 已发布 NuGet 包记录（4 包 × 4.7.0-baize.1）。 |
| `document-map.md` | 文档索引（24 行）。维护规则：新文档必须在此登记。 |

---

## 13. Gizmo 虚方法暴露

**涉及文件：** `editor/scene/3d/node_3d_editor_gizmos.cpp`、`node_3d_editor_gizmos.h`

- `can_commit_handle_on_click()` 从硬编码 `return false` 改为 `GDVIRTUAL_CALL(_can_commit_handle_on_click, ret)`。
- 新增 `GDVIRTUAL0RC(bool, _can_commit_handle_on_click)` 声明。
- 允许 GDScript Gizmo 插件覆写 handle commit-on-click 行为。

---

## 14. 杂项

### .gitignore
- 在 Python development 区新增 `.env` 条目（保护 NuGet API key）。

### AGENTS.md
- 新增项目内 AI 助手指令文件（47 行中文）。
- 定义 6 步工作流程：读代码 → 计划 → 标风险 → 实现 → 验证 → 总结。
- 编码规范：中文注释、中文 git message、命名直白通俗、不过度抽象。
- 项目长期规则：目标受众、裁剪优先级、removal-ledger.md 强制条目、验证检查清单。

### MeshLibrary 编辑器文案修正（`editor/scene/3d/mesh_library_editor_plugin.cpp`）
- `mesh_cast_shadow` 属性描述末尾补全缺失的句号。
- 空库提示语从 "by exporting them from a scene file via the Export menu" 改为 "by importing them from a scene file via the Import menu"。

---

## 15. 实验性远程分支（未合入 master）

### `origin/Left-handed-Y-UP`
- **3 commits ahead of master, 347 behind**。
- 迁移引擎至左手 Y-UP 坐标系（+X 右、+Y 上、+Z 前）。
- 修改 81 个文件（+1620/-431）：Vector3/Basis 数学、相机前方方向改为 +Z、深度处理、Gizmo 方向。
- 新增文档：`coordinate-boundaries.md`、`left-handed-y-up-coordinate-plan.md`（334 行）、`z-semantics-audit.md`（422 行）。

### `origin/Left-handed-Z-UP`
- **5 commits ahead of master, 348 behind**。
- 更激进的坐标迁移：左手 Z-UP（+X 右、+Y 前、+Z 上）。
- 修改 123 个文件（+2953/-637）：新增 `coordinate_system.h`，修改 Basis/Vector3/Vector3i 语义、网格文档、导出平台、动画编辑器等。
- 新增文档：`coordinate-system-assessment.md`（502 行）、`coordinate-system-migration-plan.md`（855 行）。

### `origin/codex-shader-editor-main-tab`
- **1 commit ahead of master, 276 behind**。
- 将着色器编辑器调整为主窗口标签页（17 文件，+166/-93）。
- 同时包含 3D 视口裁剪、导航键、Inspector、SpinBox、主题等小幅调整（与 master 上的修改重叠但独立实现）。

### `origin/csharp-support`
- **0 commits ahead of master, 131 behind**（已完全合入 master，仅落后上游合并）。
- 历史用途：C#/.NET 支持（NuGet 配置、D3D12 文档、.NET 10 目标框架、C# 构建入口清理）。
- 当前状态：stale，只需 fast-forward。

---

## 变更统计

| 类别 | 涉及文件数 | 新增文件数 |
|------|-----------|-----------|
| Inspector 重构 | 8 | 0 |
| 3D 吸附系统 | 15 | 0 |
| 3D 视口导航 | 4 | 0 |
| 场景标签优化 | 3 | 0 |
| SpinBox 属性 | 2 | 0 |
| Node3D 浮点修复 | 1 | 0 |
| Windows 路径解析 | 1 | 0 |
| C# / .NET 工具链 | 10 | 0 |
| NuGet 发布流程 | 3 | 3 |
| 构建自动化 | 10 | 10 |
| 自定义文档 | 3 | 11 |
| 品牌标识 | 4 | 1 |
| Gizmo 虚方法 | 2 | 0 |
| 杂项 | 2 | 1 |
| **合计（master）** | **~40** | **~26** |
