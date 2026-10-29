#   end-setups-tests.ps1 [-Setup <setup.exe>]
param([string]$Setup = '')

$ErrorActionPreference = 'Stop'
$script:failures = 0
function Check([string]$what, [bool]$ok, [string]$detail = '') {
    if ($ok) { Write-Output "PASS $what" } else { Write-Output "FAIL $what $detail"; $script:failures++ }
}

$tool = Join-Path $PSScriptRoot '..\..\..\tools\winix\end-setups.ps1'
. $tool -Load

$cases = @(
    @('C:\Users\winix\AppData\Local\Programs\HikariSub\Uninstall HikariSub.exe', '', '', $true),
    @('C:\Program Files\HikariSub\uninstall hikarisub.EXE', '', '', $true),
    @('C:\Users\winix\AppData\Local\Temp\~nsuA.tmp\Un_A.exe', 'HikariSub Setup', '', $true),
    @('C:\x\HikariSub-0.1.0-windows-x64-setup.exe', 'HikariSub Setup', '', $true),
    @('C:\x\setup.exe', 'HikariSub Setup ', '', $true),
    @('C:\Windows\System32\cmd.exe', '', 'Uninstall HikariSub ', $true),
    @('C:\Windows\System32\cmd.exe', '', 'Uninstall HikariSub', $true),
    @('C:\x\a.exe', '', 'HikariSub 0.1.0-test.1 Setup', $true),
    @('C:\x\a.exe', '', 'HikariSub 0.0.2 Setup ', $true),
    @('C:\x\HikariSub.exe', 'HikariSub', 's4.ass - HikariSub v0.0.1-rc.1  HEAD 20d647c ', $false),
    @('C:\x\HikariSub.exe', 'HikariSub', 'HikariSub', $false),
    @('C:\x\HikariSub.exe', '', 'Untitled - HikariSub v0.1.0 Setup', $false),
    @('C:\x\protected-test.exe', '', '', $false),
    @('C:\x\Uninstall HikariSub.exe.bak', '', '', $false),
    @('C:\x\other.exe', 'Other Setup', 'Uninstall Other', $false),
    @('C:\x\other.exe', 'hikarisub setup', 'uninstall hikarisub', $false),
    @('', '', '', $false))
foreach ($c in $cases) {
    $got = Test-HikariSetup $c[0] $c[1] $c[2]
    Check "rule: '$($c[0])' '$($c[1])' '$($c[2])' -> $($c[3])" ($got -eq $c[3]) "got $got"
}

$dir = Join-Path $env:TEMP 'hs-end-setups-test'
if (Test-Path -LiteralPath $dir) { Remove-Item -Recurse -Force -LiteralPath $dir }
New-Item -ItemType Directory -Path $dir | Out-Null
$ping = Join-Path $env:SystemRoot 'System32\PING.EXE'
Copy-Item -LiteralPath $ping -Destination (Join-Path $dir 'Uninstall HikariSub.exe')
Copy-Item -LiteralPath $ping -Destination (Join-Path $dir 'decoy.exe')
function Start-Hidden([string]$exe) {
    Start-Process -FilePath $exe -ArgumentList '-n 600 127.0.0.1' -WindowStyle Hidden -PassThru
}
function Start-Window([string]$title) {
    $code = "Add-Type -AssemblyName System.Windows.Forms; `$f = New-Object Windows.Forms.Form; `$f.Text = '$title'; " +
        "`$f.StartPosition = 'Manual'; `$f.Left = 300; `$f.Top = 200; [Windows.Forms.Application]::Run(`$f)"
    $p = Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile', '-WindowStyle', 'Hidden', '-Command', $code) -PassThru
    foreach ($i in 1..80) {
        $p.Refresh()
        if ($p.MainWindowTitle -eq $title) { break }
        Start-Sleep -Milliseconds 250
    }
    return $p
}
$ended = @(
    (Start-Hidden (Join-Path $dir 'Uninstall HikariSub.exe')),
    (Start-Window 'Uninstall HikariSub '),
    (Start-Window 'HikariSub 0.1.0-test.1 Setup'))
$kept = @(
    (Start-Hidden (Join-Path $dir 'decoy.exe')),
    (Start-Window 's4.ass - HikariSub v0.1.0'))
foreach ($p in @($ended[1], $ended[2], $kept[1])) {
    Check "stand-in window pid $($p.Id) '$($p.MainWindowTitle)' is up" ($p.MainWindowHandle -ne [IntPtr]::Zero)
}

$listed = @(& $tool -List)
foreach ($p in $ended) {
    Check "-List names pid $($p.Id)" ([bool]($listed | Where-Object { $_ -match "^running pid $($p.Id) " })) ($listed -join ' | ')
}
Check '-List ends nothing' (-not @($ended | Where-Object { $_.HasExited }).Count)

$out = @(& $tool)
$code = $LASTEXITCODE
$out | ForEach-Object { Write-Output "     $_" }
Check 'end-setups exits 0' ($code -eq 0) "exit $code"
foreach ($p in $ended) {
    Check "pid $($p.Id) ended" ($p.WaitForExit(5000))
    Check "pid $($p.Id) reported" ([bool]($out | Where-Object { $_ -match "^ended pid $($p.Id) " }))
}
foreach ($p in $kept) {
    Check "decoy pid $($p.Id) ($($p.MainWindowTitle)$($p.Path)) left running" (-not $p.HasExited)
}
$again = @(& $tool)
Check 'a second run finds none' (($LASTEXITCODE -eq 0) -and ($again -contains 'none running')) ($again -join ' | ')
$kept | Stop-Process -Force -ErrorAction SilentlyContinue

if ($Setup) {
    $p = Start-Process -FilePath $Setup -PassThru
    foreach ($i in 1..60) { $p.Refresh(); if ($p.MainWindowHandle -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 250 }
    Write-Output "     setup pid $($p.Id) window '$($p.MainWindowTitle)'"
    Check 'setup shows its first page' ($p.MainWindowHandle -ne [IntPtr]::Zero)
    $out = @(& $tool); $code = $LASTEXITCODE
    $out | ForEach-Object { Write-Output "     $_" }
    Check 'setup ended' ($code -eq 0 -and $p.WaitForExit(5000))

    # The NSIS uninstaller's %TEMP%\~nsu*.tmp copy shows the wizard.
    $instDir = Join-Path $env:LOCALAPPDATA 'Programs\HikariSubEndSetupsTest'
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub'
    if (Test-Path $key) {
        Check 'no per-user HikariSub install before the uninstaller case' $false
    } else {
        $i = Start-Process -FilePath $Setup -ArgumentList "/S /CURRENTUSER /D=$instDir" -PassThru -Wait
        Check 'installed per user' ($i.ExitCode -eq 0 -and (Test-Path -LiteralPath "$instDir\Uninstall HikariSub.exe")) "exit $($i.ExitCode)"
        $u = Start-Process -FilePath "$instDir\Uninstall HikariSub.exe" -ArgumentList '/CURRENTUSER' -PassThru
        $copy = $null
        foreach ($n in 1..80) {
            $copy = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$env:TEMP\~nsu*" -and $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
            if ($copy) { break }
            Start-Sleep -Milliseconds 250
        }
        Check "the uninstaller's copy shows its wizard" ($null -ne $copy)
        if ($copy) {
            $desc = [Diagnostics.FileVersionInfo]::GetVersionInfo($copy.Path).FileDescription
            Write-Output "     uninstaller copy pid $($copy.Id) $($copy.Path) '$($copy.MainWindowTitle)' FileDescription '$desc'"
            $out = @(& $tool); $code = $LASTEXITCODE
            $out | ForEach-Object { Write-Output "     $_" }
            Check "the uninstaller's copy ended" ($code -eq 0 -and $copy.WaitForExit(5000))
            Check "the copy was reported" ([bool]($out | Where-Object { $_ -match "^ended pid $($copy.Id) " }))
        }
        $null = $u.WaitForExit(5000)
        $null = Start-Process -FilePath "$instDir\Uninstall HikariSub.exe" -ArgumentList '/CURRENTUSER /S' -PassThru -Wait
        for ($n = 0; $n -lt 60 -and (Test-Path $key); $n++) { Start-Sleep -Milliseconds 500 }
        Check 'the test install removed' (-not (Test-Path $key))
        Remove-Item -Recurse -Force -LiteralPath $instDir -ErrorAction SilentlyContinue
    }
}

Remove-Item -Recurse -Force -LiteralPath $dir -ErrorAction SilentlyContinue
Write-Output "== $script:failures failure(s)"
if ($script:failures -gt 0) { exit 1 }
exit 0
