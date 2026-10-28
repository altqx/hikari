param([string]$Source = 'C:/Work/Kainote', [string]$Fixtures)
$prototypePython = Join-Path $env:LOCALAPPDATA 'HikariSub/prototype-runtime/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $prototypePython)) {
    throw 'The configured existing PySide6 / sounddevice prototype runtime is required. Nothing is installed by this launcher.'
}
$prototypeArgs = @((Join-Path $PSScriptRoot 'run.py'), '--source', $Source)
if ($Fixtures) { $prototypeArgs += @('--fixtures', $Fixtures) }
& $prototypePython @prototypeArgs 2> (Join-Path $PSScriptRoot 'native-stderr.log')
exit $LASTEXITCODE
