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

## 2. 准备 D3D12 依赖

定制版新项目默认使用 D3D12，所以 Windows 日常构建也默认启用 D3D12。第一次构建前先安装官方脚本需要的 D3D12 依赖：

```powershell
python misc\scripts\install_d3d12_sdk_windows.py
```

## 3. 构建编辑器

日常开发基线：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 16
```

等价的 profile 写法：

```powershell
scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j16
```

这条命令保留编辑器和 3D 运行能力，启用 D3D12，同时先关闭 AccessKit、ANGLE 这类额外依赖。

如果新机器上只是为了排查工具链，且 D3D12 依赖还没准备好，可以临时使用：

```powershell
.\misc\customization\build-windows.ps1 -Preset dev-no-d3d12 -Jobs 16
```

## 4. 运行编辑器

编译完成后运行：

```powershell
.\bin\godot.windows.editor.dev.x86_64.exe
```

也可以先看版本：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

## 5. 当前推荐

新机器第一次配置时，推荐顺序是：

1. 先安装 Python、SCons、Visual Studio C++ 工具链。
2. 运行 D3D12 依赖安装脚本。
3. 用 `dev` preset 构建。
4. 如果失败，再根据错误区分是工具链问题还是 D3D12 依赖问题；只有排查时才临时用 `dev-no-d3d12`。

这样和编辑器创建的新 3D 项目保持一致：项目默认渲染驱动是 D3D12，构建出的编辑器也应该带 D3D12。

## 6. 当前不做的事

当前阶段只专注 editor 开发定制，不维护 export template 构建、打包和发布模板。不要把 `production=yes` 或 `target=template_release` 作为日常验证命令。
