# D1 native gate on Windows (winix task gate-win-tests): the docking test
# executables under the windows platform plugin (real windows on the
# interactive desktop, not offscreen), each with QtTest's own output file,
# printed in full after a "### EXE FUNCTIONS (exit N)" line.
$ErrorActionPreference = 'Stop'
$root = $PWD.Path
$build = Join-Path $root 'out\build\windows-x64-release'
$out = Join-Path $root 'out\native-gate-win\tests'
New-Item -ItemType Directory -Force $out | Out-Null
$env:QT_QPA_PLATFORM = 'windows'
$env:PATH = "$root\out\sdk\qt-6.11.2-windows\6.11.2\msvc2022_64\bin;$env:PATH"
# The D1 functions of hikari_ui_shell_tests (as tools/native-gate/gate.py runs them).
$runs = @(
    @{ exe = 'hikari_ui_shell_tests'; fns = @('f6AndShortcutsReachAFloatingPanel', 'panelsFloatDockHideAndKeepTheirState',
            'placementWindowMovesTabsAndResizes', 'floatF6AndShowActivateTheFloatingPanelsWindow',
            'placementWindowShowsItsDefaultsAndKeyboardChanges', 'fileDropAreaLeavesPanelDragsToTheDockingEngine',
            'dockingControlsAndTheGridAreAccessible', 'floatingPanelsOffEveryScreenComeBack',
            'menuArrowsOpenAndCloseSubmenus') },
    @{ exe = 'hikari_ui_line_grid_a11y_tests'; fns = @() },
    @{ exe = 'hikari_ui_docking_qualification_tests'; fns = @() },
    @{ exe = 'hikari_ui_workspace_layout_tests'; fns = @() }
)
foreach ($run in $runs) {
    $exe = Get-ChildItem -Recurse -Filter "$($run.exe).exe" $build | Select-Object -First 1
    if (-not $exe) { Write-Output "### $($run.exe) (not built)"; continue }
    $file = Join-Path $out "$($run.exe).txt"
    $err = Join-Path $out "$($run.exe).stderr.txt"
    Remove-Item $file, $err -ErrorAction SilentlyContinue
    $arguments = @($run.fns) + @('-o', "$file,txt")
    $p = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -WorkingDirectory $exe.DirectoryName -NoNewWindow -PassThru -RedirectStandardError $err
    $null = $p.Handle
    $code = if ($p.WaitForExit(600000)) { $p.ExitCode } else { $p.Kill(); 'TIMED OUT' }
    Write-Output "### $($run.exe) $($run.fns -join ' ') (exit $code)"
    if (Test-Path $file) { Get-Content $file }
    if ((Test-Path $err) -and (Get-Item $err).Length) { Write-Output '# stderr (last 30 lines)'; Get-Content $err -Tail 30 }
}
Write-Output '### end'
