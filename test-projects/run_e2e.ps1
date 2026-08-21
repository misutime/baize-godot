# SPDX-License-Identifier: MIT
# run_e2e.ps1 —— baize-godot P1.5 headless e2e 测试基座
#
# 验证 All-in C# 最小闭环（退出条件 ①③⑥）：
#   ① 项目有 C# 工程（csproj + slnx）
#   ③ 编译运行成功
#   ⑥ headless 运行输出验证标记
#
# 用法:
#   powershell -File test-projects/run_e2e.ps1                     # 默认验证 csharp-check
#   powershell -File test-projects/run_e2e.ps1 -Project xxx         # 指定项目
#   powershell -File test-projects/run_e2e.ps1 -SkipBuild           # 跳过 dotnet build
#
# 退出码: 0 = 全部通过; 1 = 失败（输出失败详情）

param(
    [string]$Project = "csharp-check",
    [switch]$SkipBuild,
    [switch]$SkipBuildEngine
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

Write-Host "=== baize-godot e2e: $Project ==="
Write-Host "项目目录: $projDir"

# ① 工程文件存在（csproj + 解决方案 .slnx 或 .sln——新项目默认 slnx，历史项目可能 .sln）
$csproj = Get-ChildItem $projDir -Filter '*.csproj' -ErrorAction SilentlyContinue
$sln = Get-ChildItem $projDir -Include '*.slnx','*.sln' -Recurse -ErrorAction SilentlyContinue
if (-not $csproj) { $failures += "① 缺 csproj: $Project 无 C# 工程文件" }
if (-not $sln) { $failures += "① 缺解决方案: $Project 无 .slnx/.sln" }
if ($csproj -and $sln) { Write-Host "✓ 工程文件: $($csproj.Name) + $($sln.Name)" }

# ③ 编译（可选跳过）
if (-not $SkipBuild) {
    Write-Host "--- dotnet build ---"
    & dotnet build $csproj.FullName 2>&1 | Tee-Object -FilePath (Join-Path $logDir "e2e_build.log") | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $failures += "③ 编译失败 (dotnet build exit=$LASTEXITCODE)"
    } else {
        Write-Host "✓ 编译成功"
    }
}

# ⑥ headless 运行（验证标记）
if (-not (Test-Path $godotExe)) {
    $failures += "⑥ 引擎不存在: $godotExe（需先 task dev 构建）"
} else {
    Write-Host "--- headless 运行 ---"
    $outLog = Join-Path $logDir "e2e_run_$Project.log"
    $proc = Start-Process -FilePath $godotExe -ArgumentList "--path", $projDir, "--headless", "--quit-after", "3" -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outLog -RedirectStandardError (Join-Path $logDir "e2e_run_$Project.err.log")
    $output = Get-Content $outLog -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    Write-Host "exit: $($proc.ExitCode)"
    if ($proc.ExitCode -ne 0) {
        $failures += "⑥ 运行失败 (exit=$($proc.ExitCode))"
    } elseif ($output -match "验证成功") {
        Write-Host "✓ 运行成功 + 验证标记命中"
    } else {
        $failures += "⑥ 运行成功但未命中验证标记（输出见 $outLog）"
    }
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
