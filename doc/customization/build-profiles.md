# 构建配置和裁剪配置

## 当前开发基线

Windows 开发基线：

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8
```

完整的 Windows 首次配置步骤见 `getting-started-windows.md`。

也可以使用已记录的 SCons profile：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev_no_d3d12.py -j8
```

安装 D3D12 依赖后：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j8
```

或者使用脚本：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev-no-d3d12 -Jobs 8
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 8
```

含义：

- `platform=windows`：构建 Windows 编辑器。
- `dev_build=yes`：启用开发构建，便于调试源码。
- `d3d12=no`：暂时关闭 Direct3D 12 额外依赖。
- `accesskit=no`：暂时关闭屏幕阅读器支持依赖。
- `angle=no`：暂时关闭 ANGLE 依赖。
- `-j8`：使用 8 个并行任务。

macOS 也属于一等目标平台。当前仓库在 Windows 机器上先记录 Windows 基线；后续在 macOS 机器上建立对应基线后，应补充到本文件，并在同步策略中同时验证 Windows 和 macOS。

## 普通构建基线

不带 `dev_build=yes` 的普通 Windows editor 构建：

```powershell
scons platform=windows d3d12=no accesskit=no angle=no -j8
```

启用 D3D12 后：

```powershell
scons platform=windows accesskit=no angle=no -j8
```

当前阶段不维护 Windows export template 构建。先专注 editor 定制，模板构建命令暂不作为项目基线。

## 软裁剪候选命令

当前 editor 阶段，SCons 能用于裁剪的空间很小。Godot 明确禁止 editor 构建使用 `disable_3d`、`disable_advanced_gui`、`disable_physics_2d`、`disable_physics_3d`、`disable_navigation_2d`、`disable_navigation_3d`。

因此 SCons profile 只负责少数构建层开关，例如 VR/XR 和可选依赖。2D 工作区、CanvasItem 编辑工具、菜单和默认入口，主要靠 editor feature profile 或小范围源码定制处理。

每次只增加一个候选项并验证，不要一次全开。

第一轮裁剪从 VR/XR 开始，使用独立 preset：

```powershell
.\misc\customization\build-windows.ps1 -Preset prune-vr-xr -Jobs 8
```

或直接使用 profile：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_prune_vr_xr.py -j8
```

它只验证 VR/XR 裁剪，不包含 2D 物理、2D 导航等其它风险项。

软裁剪项增多后，优先维护 `misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py`，不要长期手写很长的命令。

注意：当前阶段只做 editor 软裁剪。`disable_physics_2d`、`disable_navigation_2d` 不能用于 editor 构建，只能用于 export template；因此这类选项暂时不进入任何当前阶段 profile。

运行方式：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py -j8
```

或：

```powershell
.\misc\customization\build-windows.ps1 -Preset soft-prune -Jobs 8
```

规则：

- `soft-prune` 是实验场，用来测试候选裁剪项的影响。
- `dev` 是正式开发基线，用于日常开发。
- 2D 物理/导航这类运行时裁剪项不能放进 editor 基线，当前阶段先挂起。
- 软裁剪项验证稳定后，先记录到 `removal-ledger.md`。
- 稳定项可以晋升到正式 `dev` profile，表示长期软禁用。
- 只有在依赖清楚、回滚方式明确、同步策略写清楚后，才考虑硬裁剪源码。
- 硬裁剪完成后，仍应使用 `dev` preset 构建；如果硬裁剪后只有 `soft-prune` 能过，说明裁剪边界还没整理干净。

### 2D 物理和 2D 导航

当前阶段不处理。Godot 不允许在 editor 构建里使用 `disable_physics_2d` 和 `disable_navigation_2d`，它们只能用于 export template。

### 关闭 XR 相关模块

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_prune_vr_xr.py -j8
```

## build profile 计划

Godot 支持通过 `.gdbuild` 文件记录禁用的构建选项和类。当前阶段只考虑编辑器相关 profile：

- `profiles/3d_editor_dev.gdbuild`：开发编辑器用，裁剪保守。

早期先不要提交 profile，等有一个真实 3D 项目用于检测后再生成。否则自动检测可能过度裁剪动态使用的类。

## editor feature profile 计划

Godot 的 editor feature profile 可以隐藏编辑器功能，但不会减少二进制体积，也不会删除运行时支持。

适合用于：

- 隐藏 2D 工作区。
- 隐藏非 3D 节点。
- 简化新手编辑器界面。

不适合用于：

- 减小最终 exe。
- 证明源码可以删除。
- 替代构建验证。

## 验证基线

每次修改构建配置后，至少执行：

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

手动验证：

- 启动编辑器。
- 创建或打开 3D 项目。
- 新建 3D 场景。
- 添加 MeshInstance3D、Camera3D、DirectionalLight3D。
- 运行场景。
