param(
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Python,
    [string]$Report = ''
)
$ErrorActionPreference = 'Stop'
$checkArguments = @((Join-Path $PSScriptRoot 'baize.py'), 'check', '--project', $Project, '--engine', $Engine)
if ($Report) { $checkArguments += @('--output', $Report) }
& $Python @checkArguments
exit $LASTEXITCODE
