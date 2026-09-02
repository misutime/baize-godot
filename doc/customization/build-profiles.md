# 构建配置和裁剪配置

这个文档只记录构建 profile、preset、参数含义和验证基线。第一次配置机器时，先看 `getting-started-windows.md` 或 `getting-started-macos.md`。

## 日常构建

Windows 首次开发基线：

```powershell
python misc\scripts\install_d3d12_sdk_windows.py
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16
```

Windows 日常使用构建：

```powershell
.\misc\customization\build-windows.ps1 -Preset pro -Jobs 16
```

macOS 开发基线：

```bash
./misc/customization/build-macos.sh --preset dev --jobs 10
```

macOS 日常使用构建：

```bash
./misc/customization/build-macos.sh --preset pro --jobs 10
```

## 对应 profile

Windows：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j16
scons profile=misc/customization/scons-profiles/windows_3d_pro.py -j16
```

macOS：

```bash
scons profile=misc/customization/scons-profiles/macos_3d_dev.py -j10
scons profile=misc/customization/scons-profiles/macos_3d_pro.py -j10
```

## 关键参数

- `platform=windows`：构建 Windows 编辑器。
- `platform=macos`：构建 macOS 编辑器。
- `dev_build=yes`：开发构建，适合读源码、调试和改引擎。
- `pro`：不启用 `dev_build=yes` 的编辑器构建，适合日常打开项目和体验性能。
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

Windows pro 编辑器：

```powershell
.\bin\godot.windows.editor.x86_64.exe
```

Windows 命令行版本验证：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
.\bin\godot.windows.editor.x86_64.console.exe --version
```

macOS 编辑器 app：

```bash
open bin/godot_macos_editor_dev.app
open bin/godot_macos_editor.app
```

macOS 终端日志入口：

```bash
bin/godot_macos_editor_dev.app/Contents/MacOS/Godot
```

macOS 命令行版本验证：

```bash
./bin/godot.macos.editor.dev.arm64 --version
```

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
