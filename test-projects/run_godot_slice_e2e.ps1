# SPDX-License-Identifier: MIT
# run_godot_slice_e2e.ps1 —— P2.3 Godot vertical slice 30 秒 headless 退出门禁

param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $PSScriptRoot "godot-slice"
$projectFile = Join-Path $projectDir "godot-slice.csproj"
$godotExe = Join-Path $repoRoot "bin/godot.windows.editor.dev.x86_64.mono.console.exe"
$logDir = Join-Path $repoRoot ".tmp"
$outLog = Join-Path $logDir "p23_godot_slice_e2e.log"
$errLog = Join-Path $logDir "p23_godot_slice_e2e.err.log"

$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

try {
    if (-not (Test-Path $godotExe)) {
        throw "Godot mono executable not found: $godotExe"
    }

    if (-not $SkipBuild) {
        Write-Host "--- Build godot-slice ---"
        & dotnet build $projectFile 2>&1 |
            Tee-Object -FilePath (Join-Path $logDir "p23_godot_slice_build.log") | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "dotnet build failed (exit=$LASTEXITCODE)" }
        Write-Host "Build passed"
    }

    Write-Host "--- Headless e2e (30 second timeout) ---"
    $argumentString = "--path `"$projectDir`" --headless -- --e2e"
    $job = Start-Job -ScriptBlock {
        param($exe, $arguments, $stdout, $stderr)
        cmd /c "`"$exe`" $arguments > `"$stdout`" 2> `"$stderr`""
        Write-Output "EXITCODE=$LASTEXITCODE"
    } -ArgumentList $godotExe, $argumentString, $outLog, $errLog

    if (-not (Wait-Job $job -Timeout 30)) {
        Stop-Job $job -ErrorAction SilentlyContinue
        Remove-Job $job -Force -ErrorAction SilentlyContinue
        throw "Godot headless e2e timed out after 30 seconds"
    }

    $jobOutput = Receive-Job $job
    Remove-Job $job -Force -ErrorAction SilentlyContinue
    $exitCode = -1
    foreach ($line in $jobOutput) {
        if ($line -match '^EXITCODE=(-?\d+)$') {
            $exitCode = [int]$Matches[1]
            break
        }
    }
    if ($exitCode -ne 0) {
        throw "Godot headless e2e failed (exit=$exitCode); see $outLog and $errLog"
    }

    $output = Get-Content $outLog -Raw -Encoding UTF8
    $markers = @(
        "[P23_DIRECTION_PASS]",
        "[P23_FIRE_PASS]",
        "[P23_SCORE_PASS]",
        "[P23_DEATH_PASS]",
        "[P23_ISOLATION_PASS]",
        "[P23_RESTART_PASS]",
        "[P23_SLICE_PASS]"
    )
    foreach ($marker in $markers) {
        if ($output -notmatch [regex]::Escape($marker)) {
            throw "Missing e2e gate marker: $marker (see $outLog)"
        }
    }

    Write-Host "P2.3 Godot vertical slice headless e2e passed"
    exit 0
}
catch {
    Write-Host "P2.3 Godot vertical slice e2e failed: $($_.Exception.Message)"
    exit 1
}
finally {
    Stop-Process -Name 'godot.windows*' -Force -ErrorAction SilentlyContinue
}
