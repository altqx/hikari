# Ends every HikariSub setup and uninstaller still running in the VM, with the
# windows they show: an installer test or an interactive `winix ui` session
# that stopped early can leave one open (a "Uninstall HikariSub" wizard was
# found over a desktop test's windows, 2026-10-14), and desktop tests then
# lose the focus to it or capture it in their screenshots.
#
#   end-setups.ps1          end them; prints "ended pid N ..." per process, or "none running"
#   end-setups.ps1 -List    print them, end nothing
#   . end-setups.ps1 -Load  only define Test-HikariSetup and Get-HikariSetupProcess (tests)
#
# A setup or uninstaller is a process that is (packaging/windows/hikarisub.nsi,
# legacy's and the rewrite's alike):
#   - a file named "Uninstall HikariSub.exe";
#   - a file whose version resource says FileDescription "HikariSub Setup":
#     every setup, every uninstaller and the uninstaller's own copy in
#     %TEMP%\~nsu*.tmp, whatever the file's name;
#   - a process whose main window is the wizard: title "Uninstall HikariSub" or
#     "HikariSub <version> Setup" (NSIS keeps the caption, with a trailing
#     space at times, on every page).
# The app itself (FileDescription not "HikariSub Setup", title "<doc> - HikariSub v...")
# is never one.
#
# The exit code is 0 when none is left running, 1 when one could not be ended:
# an elevated (all-users) setup cannot be ended from the desktop user's task,
# so the message names the elevated task that can.
param([switch]$List, [switch]$Load)

function Test-HikariSetup([string]$Path, [string]$Description, [string]$Title) {
    if ($Path -and ([IO.Path]::GetFileName($Path) -ieq 'Uninstall HikariSub.exe')) { return $true }
    if ($Description -and ($Description.Trim() -ceq 'HikariSub Setup')) { return $true }
    $t = if ($Title) { $Title.Trim() } else { '' }
    if ($t -ceq 'Uninstall HikariSub') { return $true }
    if ($t -cmatch '^HikariSub \S+ Setup$') { return $true }
    return $false
}

function Get-HikariSetupProcess {
    foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
        if ($p.Id -eq $PID) { continue }
        $path = $null; $description = $null; $title = $null
        try { $path = $p.Path } catch { }
        if ($path) {
            try { $description = [Diagnostics.FileVersionInfo]::GetVersionInfo($path).FileDescription } catch { }
        }
        try { $title = $p.MainWindowTitle } catch { }
        if (Test-HikariSetup $path $description $title) {
            [pscustomobject]@{ Id = $p.Id; Path = $path; Title = $title; Process = $p }
        }
    }
}

if ($Load) { return }

$found = @(Get-HikariSetupProcess)
if (-not $found.Count) { Write-Output 'none running'; exit 0 }
if ($List) {
    foreach ($s in $found) { Write-Output "running pid $($s.Id) $($s.Path) `"$($s.Title)`"" }
    exit 0
}
$left = 0
foreach ($s in $found) {
    try {
        Stop-Process -Id $s.Id -Force -ErrorAction Stop
        if (-not $s.Process.WaitForExit(10000)) { throw 'still running 10 s after it was ended' }
        Write-Output "ended pid $($s.Id) $($s.Path) `"$($s.Title)`""
    } catch {
        $why = $_.Exception.Message
        # HasExited itself throws "Access is denied" on an elevated process
        # this task cannot open: that one is still running.
        $exited = $false
        try { $exited = $s.Process.HasExited } catch { }
        if ($exited) {
            Write-Output "ended pid $($s.Id) $($s.Path) `"$($s.Title)`" (it exited by itself)"
            continue
        }
        $left++
        Write-Output "FAIL could not end pid $($s.Id) $($s.Path) `"$($s.Title)`": $why"
    }
}
if ($left) {
    Write-Output ("an elevated setup ends only from an elevated task: winix --config tests/packaging/windows/winix.yaml " +
        'run inst-admin --env ACTION=end-setups --wait')
    exit 1
}
exit 0
