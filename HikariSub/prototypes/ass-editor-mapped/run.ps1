param(
    [switch]$Offscreen,
    [switch]$SelfCheck,
    [string]$Capture,
    [ValidateRange(0, 5)][int]$Sample = 0,
    [ValidateSet('before', 'after')][string]$Affinity = 'after',
    [ValidateSet('retain', 'block')][string]$Crossing = 'retain'
)
$prototypePython = Join-Path $env:LOCALAPPDATA 'HikariSub/prototype-runtime/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $prototypePython)) {
    throw 'The existing shared PySide6 runtime was not found. Use a configured PySide6 6.11.2 Python to run prototype.py; this launcher installs nothing.'
}
$prototypeArgs = @((Join-Path $PSScriptRoot 'prototype.py'), '--sample', $Sample, '--affinity', $Affinity, '--crossing', $Crossing)
if ($Offscreen) { $prototypeArgs += '--offscreen' }
if ($SelfCheck) { $prototypeArgs += '--self-check' }
if ($Capture) { $prototypeArgs += @('--capture', $Capture) }
& $prototypePython @prototypeArgs
exit $LASTEXITCODE
