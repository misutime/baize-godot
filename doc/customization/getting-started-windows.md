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

安装 SCons：

```powershell
python -m pip install scons
```

验证：

```powershell
python --version
scons --version
```

## 2. 构建编辑器

新机器第一次构建，先跳过 D3D12：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev-no-d3d12 -Jobs 16
```

等价的 profile 写法：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev_no_d3d12.py -j16
```

这条命令保留编辑器和 3D 运行能力，同时先关闭 D3D12、AccessKit、ANGLE 这类额外依赖，方便先跑通工具链。

## 3. 运行编辑器

编译完成后运行：

```powershell
.\bin\godot.windows.editor.dev.x86_64.exe
```

也可以先看版本：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

## 4. 启用 D3D12

D3D12 是 Windows 桌面 3D 的相关能力，不建议永久删除。如果你愿意安装依赖，可以现在就启用。

安装 D3D12 依赖：

```powershell
python misc\scripts\install_d3d12_sdk_windows.py
```

然后重新构建：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16
```

等价的 profile 写法：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j16
```

## 5. 当前推荐

新机器第一次配置时，推荐顺序是：

1. 先跑最短路径，确认工具链没问题。
2. 再安装 D3D12 依赖。
3. 再用启用 D3D12 的命令重新构建。

这样排查问题最清楚：如果第一步失败，通常是 Python、SCons、Visual Studio 工具链问题；如果第二套命令失败，才重点看 D3D12 依赖。

## 6. 当前不做的事

当前阶段只专注 editor 开发定制，不维护 export template 构建、打包和发布模板。不要把 `production=yes` 或 `target=template_release` 作为日常验证命令。
