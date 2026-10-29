$ErrorActionPreference = 'Stop'
$root = $PWD.Path
$build = Join-Path $root 'out/build/windows-x64-release'
$dest = Join-Path $build 'review-window'
$env:PATH = "$root/out/sdk/qt-6.11.2-windows/6.11.2/msvc2022_64/bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'windows'
$hooks = [ordered]@{
    'k1'       = @(,@('hikari_ui_icon_tests', 'contactSheets', 'HIKARI_ICON_SHEET_DIR'))
    'k2'       = @(@('hikari_ui_theme_tests', 'swatchSheet', 'HIKARI_THEME_SHEET_DIR'),
                   @('hikari_ui_shell_tests', 'surfaceScreenshots', 'HIKARI_SURFACE_SHOT_DIR'))
    'k2-focus' = @(@('hikari_ui_theme_tests', 'panelFocusMarkContrast', 'HIKARI_THEME_SHEET_DIR'),
                   @('hikari_ui_shell_tests', 'focusScreenshots', 'HIKARI_SURFACE_SHOT_DIR'))
    'o5'       = @(,@('hikari_ui_language_tests', 'screenshots', 'HIKARI_LANGUAGE_SHOT_DIR'))
    'd3'       = @(,@('hikari_ui_shell_tests', 'dockChromeScreenshots', 'HIKARI_SURFACE_SHOT_DIR'))
}
$parts = if ($env:PARTS) { $env:PARTS -split ',' } else { @($hooks.Keys) }
$code = 0
foreach ($part in $parts) {
    if (-not $hooks.Contains($part)) { Write-Output "== ${part}: unknown part"; $code = 2; continue }
    $dir = Join-Path $dest $part
    Remove-Item -Recurse $dir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($hook in $hooks[$part]) {
        $test, $fn, $var = $hook
        $exe = Get-ChildItem -Recurse -Filter "$test.exe" $build -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $exe) { Write-Output "== $part ${test}: no executable"; $code = 1; continue }
        Set-Item "env:$var" $dir
        $out = Join-Path $dir "$test-$fn.txt"
        $p = Start-Process -FilePath $exe.FullName -ArgumentList "$fn -o `"$out,txt`"" -WorkingDirectory $exe.DirectoryName -NoNewWindow -PassThru -RedirectStandardError (Join-Path $dir "$test-$fn-stderr.txt")
        $null = $p.Handle
        if (-not $p.WaitForExit(600000)) { $p.Kill(); Write-Output "== $part $test $fn TIMED OUT"; $code = 1 }
        else { Write-Output "== $part $test $fn exit $($p.ExitCode)"; if ($p.ExitCode) { $code = 1 } }
        Remove-Item "env:$var"
        if (Test-Path $out) {
            Get-Content $out | Select-String -Pattern 'FAIL|Loc:|Actual|Expected|PASS|SKIP|QWARN|Totals'
            if (Select-String -Quiet -Pattern '^SKIP' $out) { Write-Output "== $part $fn skipped"; $code = 1 }
        } else { Write-Output "== $part ${fn}: no QtTest output"; $code = 1 }
    }
    $images = @(Get-ChildItem -Recurse -Filter *.png $dir)
    Write-Output "== $part images $($images.Count): $(($images | ForEach-Object Name) -join ', ')"
    if (-not $images.Count) { $code = 1 }
}
exit $code
