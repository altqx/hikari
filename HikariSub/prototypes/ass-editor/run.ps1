param([switch]$Offscreen, [string]$Capture)
$prototypePython = Join-Path $env:LOCALAPPDATA 'HikariSub/prototype-runtime/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $prototypePython)) {
    throw 'Install PySide6-Essentials==6.11.2 in a Python environment and run python prototype.py, or use the shared HikariSub prototype runtime.'
}
$prototypeArgs = @((Join-Path $PSScriptRoot 'prototype.py'))
if ($Offscreen) { $prototypeArgs += '--offscreen' }
if ($Capture) { $prototypeArgs += @('--capture', $Capture) }
& $prototypePython @prototypeArgs
exit $LASTEXITCODE
