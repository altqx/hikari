# D1 native gate on Windows: starts the HikariSub under test on the
# interactive desktop. gate_windows.py runs it through `winix ui launch`
# (powershell.exe -WindowStyle Hidden -File launch.ps1 [DOCUMENT]) because a
# winix task ends every process it started, and a desktop launch cannot set
# the environment: PATH gains the Qt SDK the build links (as the winix.yaml
# test tasks do) and Qt's log goes to out\native-gate-win\app-<stamp>.err.log.
param([string]$Document)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$scratch = Join-Path $root 'out\native-gate-win'
$build = Join-Path $root 'out\build\windows-x64-release'
New-Item -ItemType Directory -Force (Join-Path $scratch 'docs') | Out-Null
$exe = Get-ChildItem -Recurse -Filter hikarisub.exe $build | Sort-Object { $_.FullName -notlike '*\src\app\*' } | Select-Object -First 1
$env:PATH = "$root\out\sdk\qt-6.11.2-windows\6.11.2\msvc2022_64\bin;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = '1'
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
# hikarisub.exe is a console-subsystem program: through cmd with
# CreateNoWindow it gets no console window (Start-Process would open one in
# the default terminal, which can take the foreground).
$log = Join-Path $scratch "app-$stamp"
$doc = if ($Document) { " `"$Document`"" } else { '' }
$psi = New-Object System.Diagnostics.ProcessStartInfo 'cmd.exe'
$psi.Arguments = "/d /c `"`"$($exe.FullName)`"$doc > `"$log.out.log`" 2> `"$log.err.log`"`""
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.WorkingDirectory = Join-Path $scratch 'docs'
$p = [System.Diagnostics.Process]::Start($psi)
Set-Content -Encoding ASCII (Join-Path $scratch 'app.pid') $p.Id
