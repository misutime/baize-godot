set shell := ["bash", "-cu"]

default:
    just --list

# 构建 macOS C# 编辑器。用法：just mac-csharp dev 10
mac-csharp preset="dev" jobs="10":
    ./misc/customization/build-macos-csharp.sh --preset {{preset}} --jobs {{jobs}}

# 构建 macOS C# pro 编辑器。
mac-csharp-pro jobs="10":
    just mac-csharp pro {{jobs}}

# 构建 Windows C# 编辑器。需要在 Windows/PowerShell 环境执行。
win-csharp preset="dev" jobs="16":
    pwsh -NoProfile -ExecutionPolicy Bypass -File ./misc/customization/build-windows-csharp.ps1 -Preset {{preset}} -Jobs {{jobs}}

# 构建 Windows C# pro 编辑器。需要在 Windows/PowerShell 环境执行。
win-csharp-pro jobs="16":
    just win-csharp pro {{jobs}}

# 预览将上传的 Baize NuGet 包。
nuget-dry-run:
    python3 misc/customization/push-nuget-packages.py --dry-run

# 上传 Baize NuGet 包。
nuget-push:
    python3 misc/customization/push-nuget-packages.py --skip-duplicate