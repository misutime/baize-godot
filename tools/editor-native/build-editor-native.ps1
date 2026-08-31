# SPDX-License-Identifier: MIT
# build-editor-native.ps1 - 从 Godot 共享库构建提取 link 命令 -> 编译 wrapper -> 链接 EditorNative.dll
#
# 用法（仓库根运行）：
#   powershell -ExecutionPolicy Bypass -File tools/editor-native/build-editor-native.ps1
#   或任意目录：powershell ... -File <repo>/tools/editor-native/build-editor-native.ps1
#
# 前置：已在本仓库用下述命令完成共享库构建（等价 doc/Godot-核心编译基线.md §2）：
#   scons platform=windows target=editor library_type=shared_library \
#         module_mono_enabled=no dev_build=no d3d12=no accesskit=no angle=no debug_symbols=no
# 该构建的链接日志写 .tmp/s1-shared-child.log（含 DLL link 命令）。

param(
    [string]$RepoRoot,
    [string]$LinkLog
)

$ErrorActionPreference = 'Stop'

# ---- 1) 解析仓库根（不写死绝对路径）----
if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}
$out = Join-Path $RepoRoot '.tmp\editor-native'
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force -Path $out | Out-Null }

# vcvars（VS 路径可配，默认社区版 18）
$vcvars = $env:VCVARS64
if (-not $vcvars) { $vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat' }
if (-not (Test-Path $vcvars)) { Write-Error "vcvars64.bat not found: $vcvars (set \$env:VCVARS64 or install VS)"; exit 2 }

if (-not $LinkLog) { $LinkLog = Join-Path $RepoRoot '.tmp\s1-shared-child.log' }
if (-not (Test-Path $LinkLog)) { Write-Error "link log not found: $LinkLog (run shared_library build first)"; exit 3 }

# ---- 2) 从日志提取共享库 DLL 的最终 link 命令 ----
$text = [System.IO.File]::ReadAllText($LinkLog)
$lines = $text -split "`r?`n"
$hit = $null
foreach ($ln in $lines) {
    if ($ln -match 'godot\.windows\.editor\.x86_64\.dll' -and $ln -match '/dll') { $hit = $ln }
}
if (-not $hit) { Write-Error 'DLL link cmd not found in log'; exit 3 }
$cmd = $hit -replace '^link\s+', ''

# ---- 3) retarget to EditorNative.dll ----
$cmd = $cmd -replace '/out:bin\\godot\.windows\.editor\.x86_64\.dll', "/out:$($out -replace '\\','\\\\')\EditorNative.dll"
$cmd = $cmd -replace '/implib:bin\\godot\.windows\.editor\.x86_64\.lib', ''
$cmd = $cmd -replace '/NATVIS:platform\\windows\\godot\.natvis', ''
$cmd = $cmd + " /def:$out\EditorNative.def /MAP:$out\EditorNative.map"

# ---- 4) mono 占位替换（nomono 构建无此 lib 则不生效，保留兼容） ----
$monoLibRel = 'bin\obj\modules\module_mono.windows.editor.x86_64.lib'
$monoLibRelAlt = 'bin\obj\modules\module_mono.windows.editor.dev.x86_64.lib'
$monoLibAbs = Join-Path $RepoRoot $monoLibRel
if (Test-Path $monoLibAbs) {
    $cmd = $cmd.Replace($monoLibRel, "/WHOLEARCHIVE:$($monoLibAbs -replace '\\','\\')")
} elseif (Test-Path (Join-Path $RepoRoot $monoLibRelAlt)) {
    $monoLibAbs = Join-Path $RepoRoot $monoLibRelAlt
    $cmd = $cmd.Replace($monoLibRelAlt, "/WHOLEARCHIVE:$($monoLibAbs -replace '\\','\\')")
}

# wrapper.obj + register_module_types（模块注册链进 DLL）
$cmd = $cmd + ' ' + $out + '\wrapper.obj'
$regObj = Join-Path $RepoRoot 'bin\obj\modules\register_module_types.gen.windows.editor.x86_64.obj'
if (-not (Test-Path $regObj)) {
    $regObj = Join-Path $RepoRoot 'bin\obj\modules\register_module_types.gen.windows.editor.dev.x86_64.obj'
}
$cmd = $cmd + ' ' + $regObj

# ---- 5) 编译 wrapper（EDITOR_NATIVE_DLL=1） ----
& cmd /c "`"$vcvars`" >nul 2>&1 && cl /nologo /c /MT /O2 /std:c++17 /Zc:__cplusplus /utf-8 /D_WIN64 /DWIN32 /DEDITOR_NATIVE_DLL /I`"$RepoRoot`" /I`"$RepoRoot\platform\windows`" /Fo`"$out\wrapper.obj`" `"$out\EditorNative.cpp`""
if ($LASTEXITCODE -ne 0) { Write-Error "wrapper compile failed=$LASTEXITCODE"; exit 4 }

# ---- 6) link ----
# /OPT:NOREF keeps unreferenced registration sections (module chain) alive
$cmd = $cmd -replace '/OPT:REF', '/OPT:NOREF'
Set-Content -Path (Join-Path $out 'link.rsp') -Value $cmd -Encoding ASCII
# 保险：确保 wrapper.obj 一定在 rsp 中（若拼接未生效则追加）
$rspText = Get-Content -Raw (Join-Path $out 'link.rsp')
if ($rspText -notmatch 'wrapper\.obj') {
    Add-Content -Path (Join-Path $out 'link.rsp') -Value " $out\wrapper.obj" -Encoding ASCII
    Write-Host '[safe] appended wrapper.obj to link.rsp'
}
& cmd /c "`"$vcvars`" >nul 2>&1 && link @`"$out\link.rsp`""
if ($LASTEXITCODE -ne 0) { Write-Error "link failed=$LASTEXITCODE"; exit 5 }
Get-Item (Join-Path $out 'EditorNative.dll') | Select-Object Name, Length, LastWriteTime

# ---- 7) 断言 ----
& cmd /c "`"$vcvars`" >nul 2>&1 && dumpbin /exports `"$out\EditorNative.dll`""
& cmd /c "`"$vcvars`" >nul 2>&1 && findstr /i /c:"initialize_mono_module" /c:"register_driver_types" `"$out\EditorNative.map`""
Write-Output "--- map platform anchor ---"
& cmd /c "`"$vcvars`" >nul 2>&1 && findstr /i /c:"create_func" `"$out\EditorNative.map`""
Write-Output "build-editor-native done (review exports & anchors above)"