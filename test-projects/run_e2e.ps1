# SPDX-License-Identifier: MIT
# run_e2e.ps1 —— baize-godot P1.5 headless e2e 测试基座
#
# 验证 All-in C# 最小闭环（退出条件 ①③⑥⑤）：
#   ① 项目有 C# 工程（csproj + slnx）
#   ③ 编译运行成功
#   ⑥ headless 运行输出验证标记
#   ⑤ C# EditorPlugin 生命周期（headless editor 加载）
#
# 用法:
#   powershell -File test-projects/run_e2e.ps1                     # 默认验证 p15-check
#   powershell -File test-projects/run_e2e.ps1 -Project xxx         # 指定项目
#   powershell -File test-projects/run_e2e.ps1 -SkipBuild           # 跳过 dotnet build
#
# 退出码: 0 = 全部通过; 1 = 失败（输出失败详情）

param(
    [string]$Project = "p15-check",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$projDir = Join-Path $repoRoot "test-projects/$Project"
$godotExe = Join-Path $repoRoot "bin/godot.windows.editor.dev.x86_64.mono.console.exe"
$logDir = Join-Path $repoRoot ".tmp"
$failures = @()

# 强制 UTF-8（中文优先宪法）
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# 写日志前确保 .tmp 存在（全新 checkout 无此目录）
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

Write-Host "=== baize-godot e2e: $Project ==="
Write-Host "项目目录: $projDir"

# ① 工程文件存在（csproj + 解决方案 .slnx 或 .sln——新项目默认 slnx，历史项目可能 .sln）
$csproj = Get-ChildItem $projDir -Filter '*.csproj' -ErrorAction SilentlyContinue
$sln = Get-ChildItem $projDir -Include '*.slnx','*.sln' -Recurse -ErrorAction SilentlyContinue
if (-not $csproj) {
    $failures += "① 缺 csproj: $Project 无 C# 工程文件（检查 test-projects/ 下项目名）"
}
if (-not $sln) { $failures += "① 缺解决方案: $Project 无 .slnx/.sln" }
if ($csproj -and $sln) { Write-Host "✓ 工程文件: $($csproj.Name) + $($sln.Name)" }

# 工程缺失时跳过后续阶段（避免以空路径调用 dotnet build / godot）
$projectValid = $csproj -and $sln

# ③ 编译（可选跳过）
if ($projectValid -and -not $SkipBuild) {
    Write-Host "--- dotnet build ---"
    & dotnet build $csproj.FullName 2>&1 | Tee-Object -FilePath (Join-Path $logDir "e2e_build.log") | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $failures += "③ 编译失败 (dotnet build exit=$LASTEXITCODE)"
    } else {
        Write-Host "✓ 编译成功"
    }
}

# ⑤⑥ 前置：引擎必须存在（缺失 = 失败，不静默跳过）
if ($projectValid -and -not (Test-Path $godotExe)) {
    $failures += "引擎不存在: $godotExe（需先 task dev 构建）"
}

# 辅助：启动 Godot 子进程 + 30 秒墙钟超时（AGENTS.md 30 秒规则）
function Invoke-GodotWithTimeout {
    param(
        [string]$ArgString,
        [string]$OutLog,
        [string]$ErrLog
    )
    # Start-Job + Wait-Job -Timeout 实现 30 秒墙钟超时（AGENTS.md 30 秒规则）。
    # 退出码通过 job 输出 "EXITCODE=n" 传递（PowerShell 5.1 的 job ExitCode 属性不可靠）。
    $job = Start-Job -ScriptBlock {
        param($exe, $argsStr, $outLog, $errLog)
        # cmd /c 正确解析引号边界 + 返回进程退出码（& 会把 $argsStr 当一个参数传，导致 Godot 挂起）
        cmd /c "`"$exe`" $argsStr > `"$outLog`" 2> `"$errLog`""
        Write-Output "EXITCODE=$LASTEXITCODE"
    } -ArgumentList $godotExe, $ArgString, $OutLog, $ErrLog
    if (-not (Wait-Job $job -Timeout 30)) {
        Stop-Job $job -ErrorAction SilentlyContinue
        Remove-Job $job -Force -ErrorAction SilentlyContinue
        return @{ TimedOut = $true; ExitCode = -1 }
    }
    $jobOut = Receive-Job $job
    Remove-Job $job -Force -ErrorAction SilentlyContinue
    $exitCode = -1
    foreach ($line in $jobOut) {
        if ($line -match '^EXITCODE=(\d+)$') {
            $exitCode = [int]$Matches[1]
            break
        }
    }
    return @{ TimedOut = $false; ExitCode = $exitCode }
}

# ⑤ C# EditorPlugin 生命周期（headless editor 加载并断言插件标记 + 退出码）
if ($projectValid -and (Test-Path $godotExe)) {
    Write-Host "--- headless editor（EditorPlugin 加载断言）---"
    $editorLog = Join-Path $logDir "e2e_editor_$Project.log"
    $editorErr = Join-Path $logDir "e2e_editor_$Project.err.log"
    $editorArgs = "--path `"$projDir`" --headless --editor --quit-after 4"
    $r = Invoke-GodotWithTimeout -ArgString $editorArgs -OutLog $editorLog -ErrLog $editorErr
    $editorOutput = Get-Content $editorLog -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($r.TimedOut) {
        $failures += "⑤ EditorPlugin 超时（30 秒未退出）"
    } elseif ($r.ExitCode -ne 0) {
        $failures += "⑤ EditorPlugin 阶段退出码非零 (exit=$($r.ExitCode))"
    } elseif ($editorOutput -match "EditorPlugin 加载成功") {
        Write-Host "✓ EditorPlugin 加载成功（_EnterTree 命中，exit=0）"
    } else {
        $failures += "⑤ EditorPlugin 未加载（输出无 'EditorPlugin 加载成功'，见 $editorLog）"
    }
    Stop-Process -Name 'godot.windows*' -Force -ErrorAction SilentlyContinue
}

# ⑥ headless 运行（验证标记 + 退出码）
if ($projectValid -and (Test-Path $godotExe)) {
    Write-Host "--- headless 运行 ---"
    $outLog = Join-Path $logDir "e2e_run_$Project.log"
    $runErr = Join-Path $logDir "e2e_run_$Project.err.log"
    $runArgs = "--path `"$projDir`" --headless --quit-after 3"
    $r = Invoke-GodotWithTimeout -ArgString $runArgs -OutLog $outLog -ErrLog $runErr
    $output = Get-Content $outLog -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($r.TimedOut) {
        $failures += "⑥ 运行超时（30 秒未退出）"
    } elseif ($r.ExitCode -ne 0) {
        $failures += "⑥ 运行失败 (exit=$($r.ExitCode))"
    } elseif ($output -match "验证成功") {
        Write-Host "✓ 运行成功 + 验证标记命中"
    } else {
        $failures += "⑥ 运行成功但未命中验证标记（输出见 $outLog）"
    }
    Stop-Process -Name 'godot.windows*' -Force -ErrorAction SilentlyContinue
}

# 汇总
Write-Host ""
if ($failures.Count -eq 0) {
    Write-Host "✅ e2e 全部通过: $Project"
    exit 0
} else {
    Write-Host "❌ e2e 失败 ($($failures.Count) 项):"
    $failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
