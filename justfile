set shell := ["bash", "-cu"]
set windows-shell := ["pwsh", "-NoProfile", "-Command"]

default:
    just --list

# 构建 Windows 开发版编辑器。用法：just win-dev 16
win-dev jobs="16":
    pwsh -NoProfile -ExecutionPolicy Bypass -File ./misc/customization/build-windows.ps1 -Preset dev -Jobs {{jobs}}

# 构建 Windows pro 编辑器。
win-pro jobs="16":
    pwsh -NoProfile -ExecutionPolicy Bypass -File ./misc/customization/build-windows.ps1 -Preset pro -Jobs {{jobs}}

# 构建 macOS 开发版编辑器。用法：just mac-dev 10
mac-dev jobs="10":
    ./misc/customization/build-macos.sh --preset dev --jobs {{jobs}}

# 构建 macOS pro 编辑器。
mac-pro jobs="10":
    ./misc/customization/build-macos.sh --preset pro --jobs {{jobs}}
