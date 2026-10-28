# Runs a command inside the newest Visual Studio's x64 developer environment
# (the same environment the Windows CI job imports). Used by winix.yaml tasks.
#   ./tools/winix/msvc.ps1 cmake --workflow --preset windows-x64-verify
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the x64 C++ tools' }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
}
# Git is a declared host tool. Fall back to the Git for Windows that Visual
# Studio bundles when none is on PATH.
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    $bundled = @(
        "$vs\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd",
        "${env:ProgramFiles}\Git\cmd") | Where-Object { Test-Path "$_\git.exe" } | Select-Object -First 1
    if ($bundled) { $env:Path = "$bundled;$env:Path" }
}
$command, $rest = $args
& $command @rest
exit $LASTEXITCODE
