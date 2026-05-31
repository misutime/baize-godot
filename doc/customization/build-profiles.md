# 构建配置和裁剪配置

这个文档只记录构建 profile、preset、参数含义和验证基线。第一次配置机器时，先看 `getting-started-windows.md` 或 `getting-started-macos.md`。

## 日常构建

Windows 首次开发基线：

```powershell
python misc\scripts\install_d3d12_sdk_windows.py
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16
```

如果只是临时排查新机器依赖问题，可以先用不带 D3D12 的 fallback：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev-no-d3d12 -Jobs 16
```

macOS 开发基线：

```bash
./misc/customization/build-macos.sh --preset dev --jobs 10
```

## 对应 profile

Windows：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j16
scons profile=misc/customization/scons-profiles/windows_3d_dev_no_d3d12.py -j16
```

macOS：

```bash
scons profile=misc/customization/scons-profiles/macos_3d_dev.py -j10
```

## 关键参数

- `platform=windows`：构建 Windows 编辑器。
- `platform=macos`：构建 macOS 编辑器。
- `dev_build=yes`：开发构建，适合读源码、调试和改引擎。
- `d3d12=no`：只用于临时排查 D3D12 依赖问题，不再作为日常 Windows 3D 开发基线。
- `accesskit=no`：先关闭屏幕阅读器支持依赖。
- `angle=no`：先关闭 ANGLE 依赖。
- `vulkan=no`：macOS 先关闭 Vulkan，避免准备 MoltenVK SDK。
- `metal=yes`：macOS 使用 Metal 渲染后端。
- `generate_bundle=yes`：macOS 生成 `.app`，方便启动编辑器。

## 运行产物

Windows dev 编辑器：

```powershell
.\bin\godot.windows.editor.dev.x86_64.exe
```

Windows 命令行版本验证：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

## C# / .NET 构建

首次生成完整的 Windows C# 编辑器时，按顺序执行三步。

1. 构建带 C# / .NET 支持的编辑器：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16 module_mono_enabled=yes
```

2. 生成 C# glue：

```powershell
.\bin\godot.windows.editor.dev.x86_64.mono.console.exe --headless --generate-mono-glue modules/mono/glue
```

3. 构建 GodotSharp 托管库：

```powershell
python .\modules\mono\build_scripts\build_assemblies.py --godot-output-dir .\bin --godot-platform=windows
```

定制版默认新建和升级 C# 游戏项目到 `net10.0`。这里不改 GodotSharp 和编辑器 C# 工具自身的 `net8.0` 目标框架，它们继续作为引擎内部兼容基线。

如果只是修改编辑器 UI、菜单、面板、默认入口等界面逻辑，没有修改暴露给脚本或 C# 的 API，通常只需要重新执行第 1 步。

如果修改了 `_bind_methods()`、`ClassDB::bind_method`、属性、信号、枚举、常量、`modules/mono`、C# glue 或 GodotSharp 相关内容，需要重新执行完整三步。

Windows C# 编辑器入口：

```powershell
.\bin\godot.windows.editor.dev.x86_64.mono.exe
```

macOS 编辑器 app：

```bash
open bin/godot_macos_editor_dev.app
```

macOS 终端日志入口：

```bash
bin/godot_macos_editor_dev.app/Contents/MacOS/Godot
```

macOS 命令行版本验证：

```bash
./bin/godot.macos.editor.dev.arm64 --version
```

## 软裁剪 profile

当前 editor 阶段，SCons 能用于裁剪的空间很小。2D 工作区、CanvasItem 编辑工具、菜单和默认入口，主要靠 editor feature profile 或小范围源码定制处理。

VR/XR 软裁剪验证：

```powershell
.\misc\customization\build-windows.ps1 -Preset prune-vr-xr -Jobs 16
```

实验软裁剪场：

```powershell
.\misc\customization\build-windows.ps1 -Preset soft-prune -Jobs 16
```

规则：

- `dev` 是日常开发基线。
- `soft-prune` 只用来试候选裁剪项。
- 每次只增加一个候选项并验证。
- 稳定项先记录到 `removal-ledger.md`，再考虑晋升到正式 profile。
- 硬裁剪完成后，仍应让 `dev` profile 自然通过。

## 当前不进入 editor 基线的项

Godot 不允许 editor 构建使用这些选项：

- `disable_3d`
- `disable_advanced_gui`
- `disable_physics_2d`
- `disable_physics_3d`
- `disable_navigation_2d`
- `disable_navigation_3d`

2D 物理和 2D 导航只能作为未来 export template 裁剪项评估。当前阶段不做 export template 工作。

## 验证基线

每次修改构建配置后，至少验证对应平台能构建和启动。

Windows：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

macOS：

```bash
./misc/customization/build-macos.sh --preset dev --jobs 10
./bin/godot.macos.editor.dev.arm64 --version
open bin/godot_macos_editor_dev.app
```

手动验证：

- 启动编辑器。
- 创建或打开 3D 项目。
- 新建 3D 场景。
- 添加 MeshInstance3D、Camera3D、DirectionalLight3D。
- 运行场景。
