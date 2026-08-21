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

# ⑤ C# EditorPlugin 生命周期（headless editor 加载并断言插件标记）
if ($projectValid -and (Test-Path $godotExe)) {
    Write-Host "--- headless editor（EditorPlugin 加载断言）---"
    $editorLog = Join-Path $logDir "e2e_editor_$Project.log"
    $editorArgs = "--path `"$projDir`" --headless --editor --quit-after 4"
    $proc = Start-Process -FilePath $godotExe -ArgumentList $editorArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $editorLog -RedirectStandardError (Join-Path $logDir "e2e_editor_$Project.err.log")
    $editorOutput = Get-Content $editorLog -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($editorOutput -match "EditorPlugin 加载成功") {
        Write-Host "✓ EditorPlugin 加载成功（_EnterTree 命中）"
    } else {
        $failures += "⑤ EditorPlugin 未加载（headless editor 输出无 'EditorPlugin 加载成功'，见 $editorLog）"
    }
    # 清理 editor 进程（30 秒规则）
    Stop-Process -Name 'godot.windows*' -Force -ErrorAction SilentlyContinue
}

# ⑥ headless 运行（验证标记）
if ($projectValid -and (Test-Path $godotExe)) {
    Write-Host "--- headless 运行 ---"
    $outLog = Join-Path $logDir "e2e_run_$Project.log"
    $runArgs = "--path `"$projDir`" --headless --quit-after 3"
    $proc = Start-Process -FilePath $godotExe -ArgumentList $runArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outLog -RedirectStandardError (Join-Path $logDir "e2e_run_$Project.err.log")
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
