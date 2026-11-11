param([string]$Python = 'python', [string]$Environment = '')
$ErrorActionPreference = 'Stop'
if (-not $Environment) { $Environment = Join-Path $PSScriptRoot '.venv' }
& $Python -m venv $Environment
if ($LASTEXITCODE -ne 0) { throw 'Cannot create Python environment' }
$venvPython = Join-Path $Environment 'Scripts/python.exe'
& $venvPython -m pip install -r (Join-Path $PSScriptRoot 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Cannot install pinned dependencies' }
Write-Output "Python: $venvPython"
