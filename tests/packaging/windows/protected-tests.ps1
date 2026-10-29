# protected-tests.ps1 -Phase setup|check|cleanup|selfhold|machinedir [-Exe <protected-test.exe>] [-Setup <setup.exe>]

param(
    [Parameter(Mandatory)] [ValidateSet('setup', 'check', 'cleanup', 'machinedir', 'selfhold')] [string] $Phase,
    [string] $Exe = '',
    [string] $Setup = ''
)

$ErrorActionPreference = 'Stop'
$script:failures = 0
$Root = 'C:\HsProtected'
$ProgramFilesApp = Join-Path $env:ProgramFiles 'HsProtectedTest\app'

function Check([string] $what, [bool] $ok, [string] $detail = '') {
    if ($ok) { Write-Output "PASS $what" }
    else { Write-Output "FAIL $what $detail"; $script:failures++ }
}

function Icacls([string[]] $arguments) {
    $out = & icacls.exe @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "icacls $($arguments -join ' '): $out" }
}

function New-App([string] $dir) {
    New-Item -ItemType Directory -Force -Path "$dir\sub" | Out-Null
    Set-Content -LiteralPath "$dir\Uninstall HikariSub.exe" -Value 'x'
    Set-Content -LiteralPath "$dir\sub\file.txt" -Value 'x'
}

if ($Phase -eq 'setup') {
    if (Test-Path -LiteralPath $Root) { cmd /c rmdir /s /q $Root }
    $pf = Split-Path -Parent $ProgramFilesApp
    if (Test-Path -LiteralPath $pf) { cmd /c rmdir /s /q $pf }
    New-Item -ItemType Directory -Path $Root | Out-Null
    # C:\'s inheritable rights give Authenticated Users Modify; give users read and run only, as Program Files does.
    Icacls @($Root, '/inheritance:r', '/grant:r', '*S-1-5-32-544:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F', '*S-1-5-32-545:(OI)(CI)RX')
    New-App "$Root\locked\app"
    New-App "$Root\openfile\app"
    Icacls @("$Root\openfile\app\sub\file.txt", '/grant', '*S-1-5-32-545:M')
    # CreateFileW refuses a write to a read-only file whatever its rights.
    New-App "$Root\readonly\app"
    Icacls @("$Root\readonly\app\sub\file.txt", '/grant', '*S-1-5-32-545:M')
    Set-ItemProperty -LiteralPath "$Root\readonly\app\sub\file.txt" -Name IsReadOnly -Value $true
    New-App "$Root\opendir\app"
    Icacls @("$Root\opendir\app", '/grant', '*S-1-5-32-545:(WD)')
    New-App "$Root\parent\app"
    Icacls @("$Root\parent", '/grant', '*S-1-5-32-545:(DC,AD)')
    New-App "$Root\dacl\app"
    Icacls @("$Root\dacl", '/grant', '*S-1-5-32-545:(WDAC)')
    New-App "$Root\junction\app"
    cmd /c mklink /J "$Root\junction\app\link" "$Root\locked\app" | Out-Null
    New-App $ProgramFilesApp
    Write-Output "PASS setup made $Root and $ProgramFilesApp"
}
elseif ($Phase -eq 'check') {
    if (-not $Exe) { Write-Output 'FAIL -Exe is required'; exit 1 }
    function Answer([string] $dir) {
        $p = Start-Process -FilePath $Exe -ArgumentList "/DIR=$dir" -PassThru -Wait -WindowStyle Hidden
        return $p.ExitCode
    }
    $mine = Join-Path $env:LOCALAPPDATA 'HsProtectedTest\app'
    New-App $mine
    $cases = [ordered]@{
        $ProgramFilesApp                   = 0
        "$Root\locked\app"                 = 0
        "$Root\openfile\app"               = 1
        "$Root\readonly\app"               = 1
        "$Root\opendir\app"                = 1
        "$Root\parent\app"                 = 1
        "$Root\dacl\app"                   = 1
        "$Root\junction\app"               = 1
        $mine                              = 1
        "$Root\missing"                    = 1
        "$Root\locked\app\"                = 1
        'C:/HsProtected/locked/app'        = 1
        "$Root\locked\..\locked\app"       = 1
        "$Root\.\locked\app"               = 1
        "$Root\\locked\app"                = 1
        "\\?\$Root\locked\app"             = 1
        '\\localhost\C$\HsProtected\locked\app' = 1
    }
    foreach ($dir in $cases.Keys) {
        $got = Answer $dir
        $want = $cases[$dir]
        $what = if ($want -eq 0) { 'protected' } else { 'not protected' }
        Check "$dir is $what" ($got -eq $want) "exit $got"
    }
    Remove-Item -Recurse -Force -LiteralPath (Split-Path -Parent $mine)
    $moved = "$Root\parent\app-moved"
    try {
        Rename-Item -LiteralPath "$Root\parent\app" -NewName 'app-moved'
        Check 'the parent case: the user could move the app folder away' (Test-Path -LiteralPath $moved)
        Rename-Item -LiteralPath $moved -NewName 'app'
    } catch {
        Check 'the parent case: the user could move the app folder away' $false "$_"
    }
}
elseif ($Phase -eq 'selfhold') {
    if (-not $Setup) { Write-Output 'FAIL -Setup is required'; exit 1 }
    # PowerShell names are not case-sensitive: $exe below is $Exe.
    $harness = $Exe
    $top = Join-Path $env:TEMP 'HsSelfHoldTop'
    $dir = Join-Path $top 'HsSelfHold'
    if (Test-Path -LiteralPath $top) { Remove-Item -Recurse -Force -LiteralPath $top }
    New-Item -ItemType Directory -Path $dir | Out-Null
    function Try-RenameFolder([string] $path) { try { $name = Split-Path -Leaf $path; Rename-Item -LiteralPath $path -NewName "$name-moved" -ErrorAction Stop; Rename-Item -LiteralPath "$path-moved" -NewName $name; return $true } catch { return $false } }
    $exe = Join-Path $dir 'setup.exe'
    Copy-Item -LiteralPath $Setup -Destination $exe
    function Try-Rename { try { Rename-Item -LiteralPath $exe -NewName 'moved.exe' -ErrorAction Stop; Rename-Item -LiteralPath "$dir\moved.exe" -NewName 'setup.exe'; return $true } catch { return $false } }
    function Try-Write { try { $f = [IO.File]::Open($exe, 'Open', 'ReadWrite', 'ReadWrite'); $f.Close(); return $true } catch { return $false } }
    if ($harness) {
        $probe = Join-Path $dir 'probe.exe'
        Copy-Item -LiteralPath $harness -Destination $probe
        function Probe([string] $path) {
            $q = Start-Process -FilePath $path -ArgumentList '/HOLDSELF' -PassThru -WindowStyle Hidden
            $null = $q.Handle
            if (-not $q.WaitForExit(20000)) { Stop-Process -Id $q.Id -Force; return 'hung' }
            return $q.ExitCode
        }
        $code = Probe $probe
        Check 'from a plain folder, the file and every folder above it are held' ($code -eq 11) "got $code"
        $link = Join-Path $top 'link'
        cmd /c mklink /J $link $dir | Out-Null
        $code = Probe (Join-Path $link 'probe.exe')
        Check 'through a junction, the holds are refused' ($code -eq 10) "got $code"
        cmd /c rmdir $link | Out-Null
    }
    Check 'before setup runs, its file can be renamed' (Try-Rename)
    # No /S: setup waits on its first page.
    $p = Start-Process -FilePath $exe -PassThru
    try {
        Start-Sleep -Seconds 4
        Check 'setup is running' (-not $p.HasExited)
        $p.Refresh()
        Check 'setup shows its first page (the holds did not stall it)' ($p.MainWindowHandle -ne [IntPtr]::Zero)
        Check 'while it runs, its file cannot be renamed' (-not (Try-Rename))
        Check 'while it runs, its file cannot be opened for writing' (-not (Try-Write))
        Check 'while it runs, its file cannot be deleted' (-not $(try { Remove-Item -LiteralPath $exe -ErrorAction Stop; $true } catch { $false }))
        Check 'while it runs, its folder cannot be renamed' (-not (Try-RenameFolder $dir))
        Check 'while it runs, the folder above that cannot be renamed' (-not (Try-RenameFolder $top))
    } finally {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        $p.WaitForExit()
    }
    Start-Sleep -Seconds 1
    Check 'after it exits, its file can be renamed again' (Try-Rename)
    Check 'after it exits, its folder can be renamed again' (Try-RenameFolder $dir)
    Remove-Item -Recurse -Force -LiteralPath $top
}
elseif ($Phase -eq 'machinedir') {
    if (-not $Setup) { Write-Output 'FAIL -Setup is required'; exit 1 }
    function Install([string] $arguments) {
        $p = Start-Process -FilePath $Setup -ArgumentList $arguments -PassThru -Wait
        return $p.ExitCode
    }
    $key = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub'
    $writable = 'C:\HsWritable'
    New-Item -ItemType Directory -Force -Path $writable | Out-Null
    $pf = $env:ProgramFiles
    Get-ChildItem -LiteralPath $pf -Directory -Filter 'HikariSubTest*' | Remove-Item -Recurse -Force
    # NSIS turns "/" in /D= into "\" and drops ":" after the drive; Windows drops trailing spaces and dots ("$pf\.. \x" is "C:\x").
    $existing = "$pf\HikariSubTestExisting"
    New-Item -ItemType Directory -Force -Path $existing | Out-Null
    $dirs = @("$writable\HikariSub", "$pf\..\HsWritable\HikariSub", "$pf\.. \HsWritable\HikariSub",
        "$pf\... \..\HsWritable\HikariSub", "$pf\HikariSubTest.", "$pf", "$pf\\HikariSubTest", $existing,
        "$existing\HikariSub")
    foreach ($dir in $dirs) {
        $code = Install "/S /ALLUSERS /D=$dir"
        Check "all users in $dir refused" ($code -eq 2) "exit $code"
        Check "all users in ${dir}: nothing installed" (-not (Test-Path $key))
    }
    Check 'nothing written to the writable folder' (@(Get-ChildItem -Force -LiteralPath $writable).Count -eq 0)
    Check 'nothing written to the folder already there' (@(Get-ChildItem -Force -LiteralPath $existing).Count -eq 0)
    cmd /c rmdir /s /q $writable
    $code = Install "/S /ALLUSERS /D=$pf\HikariSubTest"
    Check "all users in $pf\HikariSubTest" ($code -eq 0) "exit $code"
    $loc = (Get-ItemProperty $key -ErrorAction SilentlyContinue).InstallLocation
    Check 'installed there' ($loc -eq "$pf\HikariSubTest") "got '$loc'"
    $code = Install "/S /ALLUSERS /D=$pf\HikariSubTest"
    Check 'updated in place' ($code -eq 0) "exit $code"
    Start-Process -FilePath "$pf\HikariSubTest\Uninstall HikariSub.exe" -ArgumentList '/ALLUSERS /S' -Wait | Out-Null
    for ($i = 0; $i -lt 60 -and (Test-Path $key); $i++) { Start-Sleep -Milliseconds 500 }
    Check 'uninstalled' (-not (Test-Path $key))
    Get-ChildItem -LiteralPath $pf -Directory -Filter 'HikariSubTest*' | Remove-Item -Recurse -Force
}
else {
    if (Test-Path -LiteralPath "$Root\junction\app\link") { cmd /c rmdir "$Root\junction\app\link" }
    if (Test-Path -LiteralPath $Root) {
        Get-ChildItem -Recurse -Force -LiteralPath $Root | ForEach-Object { $_.Attributes = 'Normal' }
        cmd /c rmdir /s /q $Root
    }
    $pf = Split-Path -Parent $ProgramFilesApp
    if (Test-Path -LiteralPath $pf) { cmd /c rmdir /s /q $pf }
    Check "cleanup removed $Root and $pf" (-not (Test-Path -LiteralPath $Root) -and -not (Test-Path -LiteralPath $pf))
}

if ($script:failures -gt 0) { exit 1 }
exit 0
