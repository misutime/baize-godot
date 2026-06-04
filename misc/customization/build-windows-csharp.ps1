param(
    [ValidateSet("dev", "pro")]
    [string] $Preset = "dev",

    [int] $Jobs = 16,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ExtraArgs
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")

Set-Location $RepoRoot

Write-Host "Step 1/3: build Windows C# editor"
& (Join-Path $ScriptDir "build-windows.ps1") -Preset $Preset -Jobs $Jobs module_mono_enabled=yes @ExtraArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Preset -eq "dev") {
    $MonoConsole = Join-Path $RepoRoot "bin\godot.windows.editor.dev.x86_64.mono.console.exe"
    $MonoConsoleFilter = "godot.windows.editor.dev*.mono.console.exe"
} else {
    $MonoConsole = Join-Path $RepoRoot "bin\godot.windows.editor.x86_64.mono.console.exe"
    $MonoConsoleFilter = "godot.windows.editor*.mono.console.exe"
}

if (-not (Test-Path $MonoConsole)) {
    $Candidates = @(Get-ChildItem -Path (Join-Path $RepoRoot "bin") -Filter $MonoConsoleFilter -File)
    if ($Preset -eq "pro") {
        $Candidates = @($Candidates | Where-Object { $_.Name -notlike "godot.windows.editor.dev*" })
    }
    $Candidates = @($Candidates | Sort-Object Name)

    if ($Candidates.Count -gt 0) {
        $MonoConsole = $Candidates[0].FullName
    } else {
        Write-Error "Cannot find Windows C# editor console binary in bin/. Expected something like: $MonoConsole"
        exit 1
    }
}

Write-Host "Step 2/3: generate C# glue with $MonoConsole"
& $MonoConsole --headless --generate-mono-glue modules/mono/glue
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Step 3/3: build GodotSharp assemblies"
& python .\modules\mono\build_scripts\build_assemblies.py --godot-output-dir .\bin --godot-platform=windows
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Windows C# editor build finished."
