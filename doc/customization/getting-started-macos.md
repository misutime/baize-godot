# macOS 从 clone 到运行

这个文档只写第一次在 macOS 上把定制 Godot 编译起来、跑起来的最短路径。构建参数细节见 `build-profiles.md`。

## 1. 准备工具

需要先安装：

- Xcode 或 Xcode Command Line Tools。
- Python 3.8 或更新版本。
- SCons。
- 如果要构建 C# / .NET 版编辑器，还需要 .NET SDK。当前定制版新建 C# 项目默认使用 `net10.0`，所以本机需要能支持 `net10.0` 的 SDK。

进入源码目录：

```bash
cd /Users/misu/misutime/godot
```

安装 SCons：

```bash
python3 -m pip install scons
```

验证：

```bash
python3 --version
scons --version
```

如果要构建 C# / .NET 版，再验证：

```bash
dotnet --info
```

## 2. 构建编辑器

如果要改引擎源码、查问题，用 `dev`：

```bash
./misc/customization/build-macos.sh --preset dev --jobs 10
```

如果要日常打开项目、体验性能，用 `pro`：

```bash
./misc/customization/build-macos.sh --preset pro --jobs 10
```

如果需要 C# / .NET 版，也同样用 `--preset` 选择版本：

```bash
# C# 开发版
./misc/customization/build-macos-csharp.sh --preset dev --jobs 10

# C# 日常使用版
./misc/customization/build-macos-csharp.sh --preset pro --jobs 10
```

C# 脚本会先构建 `.mono` 编辑器，再生成 C# glue 和 GodotSharp，最后重新生成 `.app`。

这个 preset 会使用 Metal，并先关闭 Vulkan/MoltenVK 这类额外依赖。

## 3. 运行编辑器

打开编辑器用对应的 `.app`：

```bash
# 普通 dev
open bin/godot_macos_editor_dev.app

# 普通 pro
open bin/godot_macos_editor.app

# C# dev
open bin/godot_macos_editor_dev_mono.app

# C# pro
open bin/godot_macos_editor_mono.app
```

如果只想看当前生成了哪些 `.app`：

```bash
ls bin/*.app
```

`.app` 是目录包，不要直接执行：

```bash
bin/godot_macos_editor_dev.app
```

这会得到 `permission denied`。

如果想在终端里看日志，运行对应 app 包内的可执行文件：

```bash
bin/godot_macos_editor_dev.app/Contents/MacOS/Godot
bin/godot_macos_editor.app/Contents/MacOS/Godot
bin/godot_macos_editor_dev_mono.app/Contents/MacOS/Godot
bin/godot_macos_editor_mono.app/Contents/MacOS/Godot
```

## 4. 看版本

普通版命令行验证：

```bash
# dev
./bin/godot.macos.editor.dev.arm64 --version

# pro
./bin/godot.macos.editor.arm64 --version
```

C# / .NET 版命令行验证：

```bash
# dev
./bin/godot.macos.editor.dev.arm64.mono --version

# pro
./bin/godot.macos.editor.arm64.mono --version
```

如果文件名不一致，先看实际产物：

```bash
ls bin/*mono*
```

## 5. 常用命令

```bash
# 普通编辑器
./misc/customization/build-macos.sh --preset dev --jobs 10
./misc/customization/build-macos.sh --preset pro --jobs 10

# C# / .NET 编辑器
./misc/customization/build-macos-csharp.sh --preset dev --jobs 10
./misc/customization/build-macos-csharp.sh --preset pro --jobs 10

# 打开对应版本
open bin/godot_macos_editor_dev.app
open bin/godot_macos_editor.app
open bin/godot_macos_editor_dev_mono.app
open bin/godot_macos_editor_mono.app
```

## 6. 当前不做的事

当前阶段只专注 editor 开发定制，不维护 export template 构建、打包和发布模板。
