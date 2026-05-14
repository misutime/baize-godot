param(
    [ValidateSet("dev", "dev-no-d3d12", "soft-prune")]
    [string] $Preset = "dev",

    [int] $Jobs = 8,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ExtraArgs
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")

$ProfileMap = @{
    "dev" = "misc/customization/scons-profiles/windows_3d_dev.py"
    "dev-no-d3d12" = "misc/customization/scons-profiles/windows_3d_dev_no_d3d12.py"
    "soft-prune" = "misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py"
}

$ProfilePath = $ProfileMap[$Preset]
$ArgsList = @("profile=$ProfilePath", "-j$Jobs") + $ExtraArgs

Set-Location $RepoRoot
Write-Host "Running preset '$Preset': $($ArgsList -join ' ')"

$SConsCommand = Get-Command scons -ErrorAction SilentlyContinue
if ($SConsCommand) {
    & $SConsCommand.Source @ArgsList
} else {
    & python -m SCons.Script @ArgsList
}
exit $LASTEXITCODE
