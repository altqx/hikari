$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the x64 C++ tools' }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    $bundled = @(
        "$vs\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd",
        "${env:ProgramFiles}\Git\cmd") | Where-Object { Test-Path "$_\git.exe" } | Select-Object -First 1
    if ($bundled) { $env:Path = "$bundled;$env:Path" }
}
$command, $rest = $args
& $command @rest
exit $LASTEXITCODE
