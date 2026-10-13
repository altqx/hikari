# One QtTest executable with its output in a file, printed whole (ctest keeps
# none on Windows), on the platform QT_QPA_PLATFORM names (winix tasks
# qttest-window and vsfilter-shell-all).
#   TEST          the executable's name without .exe
#   ARGS          QtTest arguments (test functions, -v2, ...)
#   TEST_TIMEOUT  seconds (300)
#   FORMAT        QtTest's output format (txt; junitxml adds each function's time)
#   BUILD         the build tree under out/build (windows-x64-release)
#   OUT           a folder the output file is also copied to (workspace-relative, optional)
#   REQUIRE_RUN   1: a skipped function fails the run (a desktop-only check that did not reach the desktop)
# Any other variable (HIKARI_*_SHOT_DIR, ...) reaches the test as it is.
$ErrorActionPreference = 'Stop'
$root = $PWD.Path
$build = Join-Path $root "out/build/$(if ($env:BUILD) { $env:BUILD } else { 'windows-x64-release' })"
$env:PATH = "$root/out/sdk/qt-6.11.2-windows/6.11.2/msvc2022_64/bin;$env:PATH"
if (-not $env:QT_QPA_PLATFORM) { $env:QT_QPA_PLATFORM = 'offscreen' }
$exe = Get-ChildItem -Recurse -Filter "$env:TEST.exe" $build -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { Write-Output "no $env:TEST.exe under $build"; exit 2 }
$limit = if ($env:TEST_TIMEOUT) { [int]$env:TEST_TIMEOUT } else { 300 }
$format = if ($env:FORMAT) { $env:FORMAT } else { 'txt' }
$out = Join-Path $env:TEMP "qttest-$env:TEST.$format"
$err = Join-Path $env:TEMP "qttest-$env:TEST-stderr.txt"
Remove-Item $out, $err -ErrorAction SilentlyContinue
$a = "$env:ARGS -o `"$out,$format`"".Trim()
Write-Output "== $($exe.FullName) $a (QT_QPA_PLATFORM=$env:QT_QPA_PLATFORM)"
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe.FullName -ArgumentList $a -WorkingDirectory $exe.DirectoryName -NoNewWindow -PassThru -RedirectStandardError $err
$null = $p.Handle
if (-not $p.WaitForExit($limit * 1000)) { $p.Kill(); Write-Output "== $env:TEST TIMED OUT after $limit s"; $code = 1 }
else { $code = $p.ExitCode; Write-Output "== $env:TEST exit $code after $([int]$sw.Elapsed.TotalSeconds) s" }
if (Test-Path $out) {
    Get-Content $out
    if ($env:OUT) {
        $dest = Join-Path $root $env:OUT
        New-Item -ItemType Directory -Force $dest | Out-Null
        Copy-Item $out $dest
        Copy-Item $err $dest -ErrorAction SilentlyContinue
    }
    if ($env:REQUIRE_RUN -eq '1' -and $format -eq 'txt' -and (Select-String -Quiet -Pattern '^SKIP' $out)) {
        Write-Output '== a function skipped, so the check did not run here'
        $code = 1
    }
} else {
    Write-Output '== no QtTest output'
    $code = if ($code) { $code } else { 1 }
}
Write-Output '== stderr'
Get-Content $err -ErrorAction SilentlyContinue | Select-Object -Last 60
exit $code
