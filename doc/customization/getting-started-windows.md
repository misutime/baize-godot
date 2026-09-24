# Windows 从 clone 到运行

这个文档只写第一次在 Windows 上把定制 Godot 编译起来、跑起来的最短路径。构建参数细节见 `build-profiles.md`。

## 1. 准备工具

需要先安装：

- Visual Studio 2022 或更新版本，并安装 C++ 桌面开发组件。
- Python 3.8 或更新版本。安装时勾选 `Add Python to PATH`。

进入源码目录：

```powershell
cd D:\misutime\godot
```

安装 SCons（Godot 的 Python 构建系统，类似 CMake，负责读取 `SConstruct` 并调度编译）：

```powershell
pip install scons
```

验证：

```powershell
python --version
scons --version
```

## 2. 准备 D3D12 依赖

定制版新项目默认使用 D3D12，Windows 构建强制要求 D3D12 依赖。第一次构建前必须安装：

```powershell
python misc\scripts\install_d3d12_sdk_windows.py
```

## 3. 构建编辑器

如果要改引擎源码、查问题，用 `dev`：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16
```

如果要日常打开项目、体验性能，用 `pro`：

```powershell
.\misc\customization\build-windows.ps1 -Preset pro -Jobs 16
```

这两个 preset 都保留编辑器和 3D 运行能力，启用 D3D12，同时先关闭 AccessKit、ANGLE 这类额外依赖。

## 4. 运行编辑器

编译完成后运行：

```powershell
# dev
.\bin\godot.windows.editor.dev.x86_64.exe

# pro
.\bin\godot.windows.editor.x86_64.exe
```

也可以先看版本：

```powershell
# dev
.\bin\godot.windows.editor.dev.x86_64.console.exe --version

# pro
.\bin\godot.windows.editor.x86_64.console.exe --version
```

## 5. 当前推荐

新机器第一次配置时，推荐顺序是：

1. 先安装 Python、SCons、Visual Studio C++ 工具链。
2. 运行 D3D12 依赖安装脚本。
3. 改引擎时用 `dev` preset；日常体验性能时用 `pro` preset。

这样和编辑器创建的新 3D 项目保持一致：项目默认渲染驱动是 D3D12，构建出的编辑器也应该带 D3D12。

## 6. 当前不做的事

当前阶段只专注 editor 开发定制，不维护 export template 构建、打包和发布模板。不要把 `production=yes` 或 `target=template_release` 作为日常验证命令。

## 7. 正式版编辑器发布

```
# 编译
scons profile=misc/customization/scons-profiles/windows_3d_pro.py -j16

# 编译完成后运行：
.\bin\godot.windows.editor.x86_64.exe

# 看版本：
.\bin\godot.windows.editor.x86_64.console.exe --version

```
