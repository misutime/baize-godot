# Windows 从 clone 到运行

这个文档记录 Windows 上第一次配置、编译、运行本定制 Godot 引擎的最短路径。

## 1. 准备工具

需要先安装：

- Visual Studio 2022 或更新版本，并安装 C++ 桌面开发组件。
- Python 3.8 或更新版本。安装时勾选 `Add Python to PATH`。

进入源码目录：

```powershell
cd D:\misutime\godot
```

安装 SCons：

```powershell
python -m pip install scons
```

验证：

```powershell
python --version
scons --version
```

## 2. 最短路径：先跳过 D3D12

如果只是想尽快编译并启动编辑器，先用这条：

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j16
```

等价的 profile 写法：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev_no_d3d12.py -j16
```

等价的脚本写法：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev-no-d3d12 -Jobs 16
```

说明：

- `dev_build=yes`：开发构建，适合读源码、调试、改引擎。
- `d3d12=no`：暂时关闭 D3D12，避免额外依赖。
- `accesskit=no`：暂时关闭屏幕阅读器支持依赖。
- `angle=no`：暂时关闭 ANGLE 依赖。
- `-j8`：使用 8 个并行任务。

编译完成后运行：

```powershell
.\bin\godot.windows.editor.dev.x86_64.exe
```

也可以先看版本：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

## 3. 启用 D3D12

D3D12 是 Windows 桌面 3D 的相关能力，不建议永久删除。如果你愿意安装依赖，可以现在就启用。

安装 D3D12 依赖：

```powershell
python misc\scripts\install_d3d12_sdk_windows.py
```

然后重新构建，去掉 `d3d12=no`：

```powershell
scons platform=windows dev_build=yes accesskit=no angle=no -j8
```

等价的 profile 写法：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j8
```

等价的脚本写法：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 8
```

运行：

```powershell
.\bin\godot.windows.editor.dev.x86_64.exe
```

## 4. 当前推荐

新机器第一次配置时，推荐顺序是：

1. 先跑最短路径，确认工具链没问题。
2. 再安装 D3D12 依赖。
3. 再用启用 D3D12 的命令重新构建。

这样排查问题最清楚：如果第一步失败，通常是 Python、SCons、Visual Studio 工具链问题；如果第二套命令失败，才重点看 D3D12 依赖。

## 5. 非 dev_build 构建

`dev_build=yes` 适合开发引擎源码。它会启用开发调试相关代码，方便断点和排查问题，但不是最终发布时最接近用户环境的构建方式。

如果只是想编一个普通编辑器，不加 `dev_build=yes`：

```powershell
scons platform=windows d3d12=no accesskit=no angle=no -j8
```

如果已经安装 D3D12 依赖：

```powershell
scons platform=windows accesskit=no angle=no -j8
```

## 6. 导出模板

当前阶段不做 export template 相关工作。先专注编辑器开发定制，不维护打包游戏用的模板构建命令。

## 7. production 构建

真正面向发布时，可以考虑 `production=yes`。它会更偏向正式发布配置，通常会更慢，排查问题也没 `dev_build=yes` 方便。

普通发布模板以后再单独建立规则。当前阶段不要把 `production=yes` 或 `target=template_release` 作为日常验证命令。
