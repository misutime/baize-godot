# 发布 C# NuGet 包

定制版 C# 编辑器会构建一组 NuGet 包，用户项目的 `.csproj` 通过这些包获得 Godot C# SDK、API 和源码生成器。

当前包名前缀为 `Baize`：

```text
Baize.Godot.NET.Sdk
Baize.GodotSharp
Baize.GodotSharpEditor
Baize.Godot.SourceGenerators
```

用户项目生成时会引用：

```xml
<Project Sdk="Baize.Godot.NET.Sdk/包版本">
```

`包版本` 来自本次 C# assemblies 构建生成的 `modules/mono/SdkPackageVersions.props`。

发布版本由 `version.py` 的 `status` 管理。建议使用带数字的定制状态：

```python
status = "baize1"
```

构建脚本会把它转成：

```text
4.7.0-baize.1
```

下一次发布前递增为 `baize2`、`baize3`。nuget.org 不允许覆盖同名同版本包，所以内容有变化时必须递增版本。

## 1. 构建包

先完成 C# 编辑器构建。macOS 示例：

```bash
./misc/customization/build-macos-csharp.sh --preset dev --jobs 10
```

Windows 示例：

```powershell
.\misc\customization\build-windows-csharp.ps1 -Preset dev -Jobs 10
```

构建完成后，NuGet 包位于：

```text
bin/GodotSharp/Tools/nupkgs
```

## 2. 预览将要上传的包

先 dry-run，确认包名和版本：

```bash
python3 misc/customization/push-nuget-packages.py --dry-run
```

如果要同时查看符号包：

```bash
python3 misc/customization/push-nuget-packages.py --dry-run --include-symbols
```

## 3. 上传到 nuget.org

不要把 API key 写进仓库。推荐写到仓库根目录的 `.env`：

```text
NUGET_API_KEY=你的 NuGet API key
```

`.env` 已加入 git 忽略，脚本会自动读取它。系统环境变量优先级更高，可以临时覆盖 `.env`。

上传普通包：

```bash
python3 misc/customization/push-nuget-packages.py --skip-duplicate
```

脚本默认只上传 `Baize.` 前缀的包，避免目录里残留官方 `Godot.*` 包时误传。

如果还要上传 `.snupkg` 符号包：

```bash
python3 misc/customization/push-nuget-packages.py --include-symbols --skip-duplicate
```

## 4. 注意事项

nuget.org 不允许覆盖已上传的同名同版本包。版本号一旦发布错了，只能换新版本重新发布。

上传脚本会维护发布清单：

```text
doc/customization/nuget-publish-manifest.json
```

每次成功上传后，清单会记录包名、版本、整包 sha256 和有效内容 sha256。下次上传前脚本会检查有效内容：

- 同版本同内容：提示无需重复上传。
- 同版本但内容变化：拒绝上传，要求先递增 `version.py` 的 `status`。
- 新版本或新包：允许上传。

有效内容校验会忽略 NuGet 签名、ZIP 容器元数据和 `package/services/metadata/core-properties`。这些内容会因为重新构建或 nuget.org 仓库签名而变化，不代表 C# SDK/API 内容真的改变。

首次发布 `Baize.*` 包时，建议在 nuget.org 申请 `Baize` 包名前缀，避免别人占用相近包名。

如果只是在本机测试，不需要上传到 nuget.org，可以使用本地 NuGet 源：

```bash
dotnet nuget add source ~/MyLocalNugetSource --name MyLocalNugetSource
./modules/mono/build_scripts/build_assemblies.py --godot-output-dir ./bin --push-nupkgs-local ~/MyLocalNugetSource
```

本地源只适合开发调试，不适合团队分发。
