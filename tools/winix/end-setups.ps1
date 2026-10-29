param([switch]$List, [switch]$Load)

# NSIS runs the uninstaller from a renamed %TEMP%\~nsu*.tmp copy and sometimes pads wizard captions with a space.
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
        # HasExited throws "Access is denied" on an elevated process that is still running.
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
