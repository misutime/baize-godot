# macOS 从 clone 到运行

这个文档只写第一次在 macOS 上把定制 Godot 编译起来、跑起来的最短路径。构建参数细节见 `build-profiles.md`。

## 1. 准备工具

需要先安装：

- Xcode 或 Xcode Command Line Tools。
- Python 3.8 或更新版本。
- SCons。

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

## 2. 构建编辑器

日常开发先用项目脚本：

```bash
./misc/customization/build-macos.sh --preset dev --jobs 10
```

等价的 profile 写法：

```bash
scons profile=misc/customization/scons-profiles/macos_3d_dev.py -j10
```

这个 preset 会使用 Metal，并先关闭 Vulkan/MoltenVK 这类额外依赖。

## 3. 运行编辑器

构建完成后，编辑器 app 位于：

```bash
bin/godot_macos_editor_dev.app
```

`.app` 是目录包，不是普通命令文件。启动编辑器用：

```bash
open bin/godot_macos_editor_dev.app
```

如果想在终端里看日志，运行包内可执行文件：

```bash
bin/godot_macos_editor_dev.app/Contents/MacOS/Godot
```

不要直接执行 `.app` 目录：

```bash
bin/godot_macos_editor_dev.app
```

这会得到 `permission denied`。

## 4. 看版本

当前 Apple Silicon dev 构建通常会生成这个命令行二进制：

```bash
./bin/godot.macos.editor.dev.arm64 --version
```

它适合做快速验证。真正打开编辑器时，优先用 `.app`。

## 5. 当前不做的事

当前阶段只专注 editor 开发定制，不维护 export template 构建、打包和发布模板。
