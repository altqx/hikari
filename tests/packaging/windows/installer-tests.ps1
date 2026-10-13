# HikariSub NSIS installer checks on Windows (installers spec, 8.3).
#
#   installer-tests.ps1 -Phase user|machine|semver -SetupDir <dir> [-Old 0.0.2] [-New 0.0.3]
#
# <dir> holds two installers of the same codebase, HikariSub-<Old>-windows-x64-setup.exe
# and HikariSub-<New>-windows-x64-setup.exe, built from payloads that differ: the
# new one lacks Themes\ and HikariSub_AVX.exe/.pdb and adds NewInB.txt and
# Extra\sub\file.txt (tests/packaging/windows/make-test-payloads.sh).
#
# user:    run as a standard user. Per-user install, upgrade with the app running
#          (exit 3), /UPDATE with /WAITPID and /RELAUNCH, downgrade (exit 5),
#          scope errors (4, 6), associations next to another app's, uninstall.
# machine: run elevated (SYSTEM through Winix's elevated task, no desktop).
#          All-users install, upgrade, /UPDATE, downgrade, scope conflict, uninstall.
# semver:  run as a standard user, with <dir> from make-semver-installers.sh:
#          installs a sequence of versions per user and checks that each one
#          that is older than the installed one is refused (SemVer precedence).
#
# Prints PASS/FAIL per check and exits 1 if any check failed. It removes what it
# installed, but leaves files a user would own (it creates and deletes its own).

param(
    [Parameter(Mandatory)] [ValidateSet('user', 'machine', 'semver')] [string] $Phase,
    [Parameter(Mandatory)] [string] $SetupDir,
    [string] $Old = '0.0.2',
    [string] $New = '0.0.3'
)

$ErrorActionPreference = 'Stop'
$script:failures = 0
$OldSetup = Join-Path $SetupDir "HikariSub-$Old-windows-x64-setup.exe"
$NewSetup = Join-Path $SetupDir "HikariSub-$New-windows-x64-setup.exe"
$Mutex = 'Global\io.github.altqx.HikariSub.Running'
$Exts = 'ass', 'ssa', 'srt', 'sub'
$IconIds = @{ ass = 109; ssa = 110; srt = 111; sub = 112 }

function Check([string] $what, [bool] $ok, [string] $detail = '') {
    if ($ok) { Write-Output "PASS $what" }
    else { Write-Output "FAIL $what $detail"; $script:failures++ }
}

function Run-Setup([string] $exe, [string] $arguments) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    # Not Start-Process -Wait: Windows PowerShell then also waits for the
    # processes the installer starts, such as the relaunched app.
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $null = $p.Handle
    $p.WaitForExit()
    [Console]::WriteLine("  ran $(Split-Path -Leaf $exe) $arguments -> $($p.ExitCode) in $([int]$sw.Elapsed.TotalSeconds) s")
    return $p.ExitCode
}

# Runs the uninstaller the way Settings > Apps does (it copies itself to %TEMP%
# and returns at once), then waits for the Uninstall key to go.
function Run-Uninstaller([string] $dir, [string] $arguments, [string] $root) {
    $exe = Join-Path $dir 'Uninstall HikariSub.exe'
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -Wait
    Write-Output "  ran uninstaller $arguments -> launcher $($p.ExitCode)"
    $key = "${root}:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub"
    for ($i = 0; $i -lt 120; $i++) {
        if (-not (Test-Path $key) -and -not (Test-Path $exe)) { break }
        Start-Sleep -Milliseconds 500
    }
    Start-Sleep -Seconds 2
}

function RegValue([string] $key, [string] $name) {
    $k = Get-Item -LiteralPath $key -ErrorAction SilentlyContinue
    if (-not $k) { return $null }
    return $k.GetValue($name, $null, 'DoNotExpandEnvironmentNames')
}

function Shortcut-Info([string] $lnk) {
    $target = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath
    $folder = (New-Object -ComObject Shell.Application).Namespace((Split-Path $lnk))
    $aumid = $folder.ParseName((Split-Path -Leaf $lnk)).ExtendedProperty('System.AppUserModel.ID')
    return @{ Target = $target; Aumid = $aumid }
}

function Manifest-Files([string] $dir) {
    return @(Get-Content -LiteralPath (Join-Path $dir 'install-manifest.txt') | Where-Object { $_ })
}

function Check-Installed([string] $dir, [string] $root, [string] $kind, [string] $version, [string] $programs, [string] $components) {
    $files = Manifest-Files $dir
    $missing = @($files | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dir $_)) })
    Check "$version payload present ($($files.Count) files)" ($files.Count -gt 0 -and $missing.Count -eq 0) "missing: $($missing -join ', ')"
    $sorted = $true
    for ($n = 1; $n -lt $files.Count; $n++) {
        $cmp = [string]::CompareOrdinal($files[$n - 1].ToLowerInvariant(), $files[$n].ToLowerInvariant())
        if ($cmp -gt 0 -or ($cmp -eq 0 -and [string]::CompareOrdinal($files[$n - 1], $files[$n]) -ge 0)) { $sorted = $false }
    }
    Check "$version manifest is sorted, relative, files only" ($sorted -and -not ($files | Where-Object { $_ -match '^[\\/]|:|\.\.' }))
    Check "$version package-kind.txt = $kind" ((Get-Content -Raw -LiteralPath (Join-Path $dir 'package-kind.txt')).Trim() -ceq $kind)
    Check "$version uninstaller present" (Test-Path -LiteralPath (Join-Path $dir 'Uninstall HikariSub.exe'))

    $u = "${root}:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub"
    $flag = if ($kind -eq 'nsis-machine') { '/ALLUSERS' } else { '/CURRENTUSER' }
    $scope = if ($kind -eq 'nsis-machine') { 'machine' } else { 'user' }
    $expect = [ordered]@{
        DisplayName = 'HikariSub'; DisplayVersion = $version; Publisher = 'altqx'
        DisplayIcon = "`"$dir\HikariSub.exe`",0"; InstallLocation = $dir
        UninstallString = "`"$dir\Uninstall HikariSub.exe`" $flag"
        QuietUninstallString = "`"$dir\Uninstall HikariSub.exe`" $flag /S"
        URLInfoAbout = 'https://github.com/altqx/hikari'; HelpLink = 'https://github.com/altqx/hikari/issues'
        NoModify = 1; NoRepair = 1; InstallScope = $scope
    }
    foreach ($name in $expect.Keys) {
        $v = RegValue $u $name
        Check "$version Uninstall\$name" ($v -eq $expect[$name]) "got '$v'"
    }
    Check "$version Uninstall\EstimatedSize" ((RegValue $u 'EstimatedSize') -gt 0)
    $i = "${root}:\Software\HikariSub\Install"
    Check "$version Install\Version" ((RegValue $i 'Version') -eq $version)
    Check "$version Install\InstallLocation" ((RegValue $i 'InstallLocation') -eq $dir)
    Check "$version Install\InstallScope" ((RegValue $i 'InstallScope') -eq $scope)
    Check "$version Install\Components = '$components'" ((RegValue $i 'Components') -eq $components) "got '$(RegValue $i 'Components')'"
    $ap = "${root}:\Software\Microsoft\Windows\CurrentVersion\App Paths\HikariSub.exe"
    Check "$version App Paths" ((RegValue $ap '') -eq "$dir\HikariSub.exe" -and (RegValue $ap 'Path') -eq $dir)

    $lnk = Join-Path $programs 'HikariSub.lnk'
    if (Test-Path -LiteralPath $lnk) {
        $info = Shortcut-Info $lnk
        Check "$version Start menu shortcut target" ($info.Target -eq "$dir\HikariSub.exe") "got '$($info.Target)'"
        Check "$version Start menu shortcut AppUserModelID" ($info.Aumid -eq 'io.github.altqx.HikariSub') "got '$($info.Aumid)'"
    } else { Check "$version Start menu shortcut $lnk" $false }
}

function Check-Associations([string] $dir, [string] $root, [string] $label, [hashtable] $foreignDefaults) {
    $c = "${root}:\Software\Classes"
    foreach ($e in $Exts) {
        $prog = "$c\HikariSub.$e"
        Check "$label ProgID HikariSub.$e" ((RegValue $prog '') -eq "$($e.ToUpper()) subtitles")
        Check "$label HikariSub.$e DefaultIcon" ((RegValue "$prog\DefaultIcon" '') -eq "`"$dir\HikariSub.exe`",-$($IconIds[$e])") "got '$(RegValue "$prog\DefaultIcon" '')'"
        Check "$label HikariSub.$e open command" ((RegValue "$prog\shell\open\command" '') -eq "`"$dir\HikariSub.exe`" `"%1`"")
        $owp = Get-Item -LiteralPath "$c\.$e\OpenWithProgids" -ErrorAction SilentlyContinue
        Check "$label .$e OpenWithProgids has HikariSub.$e (REG_NONE)" ($owp -and $owp.GetValueNames() -contains "HikariSub.$e" -and $owp.GetValueKind("HikariSub.$e") -eq 'None')
        $want = if ($foreignDefaults.ContainsKey($e)) { $foreignDefaults[$e] } else { "HikariSub.$e" }
        Check "$label .$e default = $want" ((RegValue "$c\.$e" '') -eq $want) "got '$(RegValue "$c\.$e" '')'"
        Check "$label SupportedTypes .$e" ((Get-Item -LiteralPath "$c\Applications\HikariSub.exe\SupportedTypes" -ErrorAction SilentlyContinue).GetValueNames() -contains ".$e")
        Check "$label Capabilities .$e" ((RegValue "${root}:\Software\HikariSub\Capabilities\FileAssociations" ".$e") -eq "HikariSub.$e")
    }
    Check "$label never .txt" (-not (Test-Path -LiteralPath "$c\HikariSub.txt") -or (RegValue "$c\HikariSub.txt\shell\open\command" '') -ne "`"$dir\HikariSub.exe`" `"%1`"")
    Check "$label RegisteredApplications" ((RegValue "${root}:\Software\RegisteredApplications" 'HikariSub') -eq 'Software\HikariSub\Capabilities')
    Check "$label Capabilities name" ((RegValue "${root}:\Software\HikariSub\Capabilities" 'ApplicationName') -eq 'HikariSub')
}

function Check-Gone([string] $dir, [string] $root, [string] $programs, [string] $desktop, [hashtable] $foreignDefaults, [string[]] $userFiles) {
    $c = "${root}:\Software\Classes"
    Check 'uninstall: Uninstall key gone' (-not (Test-Path "${root}:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub"))
    Check 'uninstall: Install key gone' (-not (Test-Path "${root}:\Software\HikariSub\Install"))
    Check 'uninstall: Capabilities gone' (-not (Test-Path "${root}:\Software\HikariSub\Capabilities"))
    Check 'uninstall: RegisteredApplications value gone' ($null -eq (RegValue "${root}:\Software\RegisteredApplications" 'HikariSub'))
    Check 'uninstall: App Paths gone' (-not (Test-Path "${root}:\Software\Microsoft\Windows\CurrentVersion\App Paths\HikariSub.exe"))
    Check 'uninstall: Applications\HikariSub.exe gone' (-not (Test-Path "$c\Applications\HikariSub.exe"))
    foreach ($e in $Exts) {
        Check "uninstall: ProgID HikariSub.$e gone" (-not (Test-Path -LiteralPath "$c\HikariSub.$e"))
        $owp = Get-Item -LiteralPath "$c\.$e\OpenWithProgids" -ErrorAction SilentlyContinue
        Check "uninstall: .$e OpenWithProgids entry gone" (-not $owp -or $owp.GetValueNames() -notcontains "HikariSub.$e")
        $want = if ($foreignDefaults.ContainsKey($e)) { $foreignDefaults[$e] } else { $null }
        Check "uninstall: .$e default = '$want'" ((RegValue "$c\.$e" '') -eq $want) "got '$(RegValue "$c\.$e" '')'"
    }
    Check 'uninstall: Start menu shortcut gone' (-not (Test-Path -LiteralPath (Join-Path $programs 'HikariSub.lnk')))
    Check 'uninstall: desktop shortcut gone' (-not (Test-Path -LiteralPath (Join-Path $desktop 'HikariSub.lnk')))
    if ($userFiles.Count -eq 0) {
        Check "uninstall: $dir gone" (-not (Test-Path -LiteralPath $dir)) "left: $((Get-ChildItem -Recurse -Force -LiteralPath $dir -ErrorAction SilentlyContinue | ForEach-Object FullName) -join ', ')"
    } else {
        $left = @(Get-ChildItem -Recurse -Force -File -LiteralPath $dir | ForEach-Object { $_.FullName.Substring($dir.Length + 1) })
        $extra = @($left | Where-Object { $userFiles -notcontains $_ })
        $lost = @($userFiles | Where-Object { $left -notcontains $_ })
        Check 'uninstall: only user files remain' ($extra.Count -eq 0) "extra: $($extra -join ', ')"
        Check 'uninstall: user files kept' ($lost.Count -eq 0) "lost: $($lost -join ', ')"
    }
}

function Check-Upgraded([string] $dir, [string] $label) {
    Check "$label orphan Themes payload removed" (-not (Test-Path -LiteralPath "$dir\Themes\DeepDark.txt"))
    Check "$label orphan HikariSub_AVX.exe removed" (-not (Test-Path -LiteralPath "$dir\HikariSub_AVX.exe"))
    Check "$label orphan HikariSub_AVX.pdb removed" (-not (Test-Path -LiteralPath "$dir\HikariSub_AVX.pdb"))
    Check "$label new files present" ((Test-Path -LiteralPath "$dir\NewInB.txt") -and (Test-Path -LiteralPath "$dir\Extra\sub\file.txt"))
    $files = Manifest-Files $dir
    Check "$label manifest is the new payload" ($files -contains 'NewInB.txt' -and $files -notcontains 'HikariSub_AVX.exe')
}

function Stop-Installed([string] $dir) {
    Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith("$dir\", 'OrdinalIgnoreCase') } | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

function Remove-Existing([string] $root, [string] $flag) {
    $loc = RegValue "${root}:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub" 'InstallLocation'
    if ($loc -and (Test-Path -LiteralPath "$loc\Uninstall HikariSub.exe")) {
        Write-Output "  removing an earlier install in $loc"
        Stop-Installed $loc
        Run-Uninstaller $loc "$flag /S" $root
    }
}

# Version, then the exit code installing it over the one before must give.
$SemverSequence = @(
    @('0.0.3-rc.2', 0), @('0.0.3-rc.10', 0), @('0.0.3-rc.9', 5), @('0.0.3', 0),
    @('0.0.3-rc.10', 5), @('0.0.10', 0), @('0.0.9', 5), @('0.1.0-alpha', 0),
    @('0.1.0-alpha.1', 0), @('0.1.0-alpha.beta', 0), @('0.1.0-beta', 0),
    @('0.1.0-BETA.1', 5), @('0.1.0-beta.11', 0), @('0.1.0-rc.1', 0), @('0.1.0', 0),
    @('0.1.0', 0), @('0.1.0-rc.1', 5))

if ($Phase -ne 'semver') {
    foreach ($f in $OldSetup, $NewSetup) {
        if (-not (Test-Path -LiteralPath $f)) { Write-Output "FAIL missing $f"; exit 1 }
    }
}

if ($Phase -eq 'semver') {
    $dir = Join-Path $env:LOCALAPPDATA 'Programs\HikariSub'
    Write-Output "== semver phase as $env:USERNAME, $dir"
    Remove-Existing 'HKCU' '/CURRENTUSER'
    $installed = $null
    try {
        foreach ($step in $SemverSequence) {
            $v = $step[0]
            $code = Run-Setup (Join-Path $SetupDir "HikariSub-$v-windows-x64-setup.exe") '/S /CURRENTUSER /COMPONENTS='
            Check "install $v over ${installed}: exit $($step[1])" ($code -eq $step[1]) "got $code"
            if ($code -eq 0) { $installed = $v }
            $now = RegValue 'HKCU:\Software\HikariSub\Install' 'Version'
            Check "installed version is $installed" ($now -ceq $installed) "got $now"
        }
    } finally {
        Remove-Existing 'HKCU' '/CURRENTUSER'
    }
    Check 'semver: uninstalled' (-not (Test-Path -LiteralPath $dir))
} elseif ($Phase -eq 'user') {
    $root = 'HKCU'
    $dir = Join-Path $env:LOCALAPPDATA 'Programs\HikariSub'
    $programs = [Environment]::GetFolderPath('Programs')
    $desktop = [Environment]::GetFolderPath('Desktop')
    $c = 'HKCU:\Software\Classes'
    Write-Output "== per-user phase as $env:USERNAME, $dir"
    Remove-Existing 'HKCU' '/CURRENTUSER'
    if (Test-Path -LiteralPath $dir) { Remove-Item -Recurse -Force -LiteralPath $dir }

    # Another app owns .srt: the installer must not take it.
    $savedSrt = RegValue "$c\.srt" ''
    if ($savedSrt -eq 'OtherApp.srt') { $savedSrt = $null }  # left by an interrupted run
    New-Item -Force -Path "$c\.srt" | Out-Null
    Set-ItemProperty -LiteralPath "$c\.srt" -Name '(default)' -Value 'OtherApp.srt'
    foreach ($e in 'ass', 'ssa', 'sub') {
        if ((RegValue "$c\.$e" '') -like 'HikariSub.*') { Remove-ItemProperty -LiteralPath "$c\.$e" -Name '(default)' }
    }
    $foreign = @{ srt = 'OtherApp.srt' }
    try {
        Check 'all users without admin: exit 4' ((Run-Setup $OldSetup '/S /ALLUSERS') -eq 4)
        Check 'fresh per-user install: exit 0' ((Run-Setup $OldSetup '/S /CURRENTUSER') -eq 0)
        Check-Installed $dir 'HKCU' 'nsis-user' $Old $programs 'assoc'
        Check-Associations $dir 'HKCU' $Old $foreign
        Check 'desktop shortcut off by default' (-not (Test-Path -LiteralPath "$desktop\HikariSub.lnk"))
        Check 'scope conflict: exit 6' ((Run-Setup $NewSetup '/S /ALLUSERS') -eq 6)
        Check 'reinstall of the same version: exit 0' ((Run-Setup $OldSetup '/S') -eq 0)

        # Files a user put there.
        New-Item -ItemType Directory -Force "$dir\Config" | Out-Null
        Set-Content -LiteralPath "$dir\Config\user.txt" 'mine'
        Set-Content -LiteralPath "$dir\Themes\Mine.txt" 'mine'

        # The app running from the install folder blocks the update.
        $app = Start-Process -FilePath "$dir\HikariSub.exe" -PassThru
        Start-Sleep -Seconds 5
        Check 'silent upgrade while HikariSub runs: exit 3' ((Run-Setup $NewSetup '/S /CURRENTUSER') -eq 3)
        $result = Join-Path $env:LOCALAPPDATA 'HikariSub\Updates\last-result.json'
        Remove-Item -Force -LiteralPath $result -ErrorAction SilentlyContinue
        Check '/UPDATE with /WAITPID of a running app: exit 3' ((Run-Setup $NewSetup "/S /UPDATE /WAITPID=$($app.Id) /CURRENTUSER /D=$dir") -eq 3)
        $json = if (Test-Path -LiteralPath $result) { Get-Content -Raw -LiteralPath $result | ConvertFrom-Json } else { $null }
        Check '/UPDATE failure writes last-result.json' ($json -and $json.version -eq $New -and $json.ok -eq $false -and $json.code -eq 3) "got $(if ($json) { $json | ConvertTo-Json -Compress })"
        Check 'old version untouched after exit 3' ((RegValue 'HKCU:\Software\HikariSub\Install' 'Version') -eq $Old)
        Stop-Installed $dir

        # The named mutex alone also blocks it.
        $m = [Threading.Mutex]::new($false, $Mutex)
        Check 'silent upgrade while the mutex exists: exit 3' ((Run-Setup $NewSetup '/S') -eq 3)
        $m.Dispose()
        Remove-Item -Force -LiteralPath $result -ErrorAction SilentlyContinue

        # /UPDATE waits for /WAITPID, installs, then relaunches the app.
        $waiter = Start-Process -FilePath powershell.exe -ArgumentList '-NoProfile -Command Start-Sleep -Seconds 8' -PassThru -WindowStyle Hidden
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $code = Run-Setup $NewSetup "/S /UPDATE /WAITPID=$($waiter.Id) /RELAUNCH /CURRENTUSER /D=$dir"
        Check '/UPDATE /WAITPID /RELAUNCH: exit 0' ($code -eq 0)
        Check '/UPDATE waited for /WAITPID' ($waiter.HasExited -and $sw.Elapsed.TotalSeconds -ge 6)
        Check '/UPDATE success writes no last-result.json' (-not (Test-Path -LiteralPath $result))
        Check-Installed $dir 'HKCU' 'nsis-user' $New $programs 'assoc'
        Check-Upgraded $dir 'upgrade'
        Check-Associations $dir 'HKCU' $New $foreign
        Check 'upgrade kept user files' ((Test-Path -LiteralPath "$dir\Config\user.txt") -and (Test-Path -LiteralPath "$dir\Themes\Mine.txt"))
        Start-Sleep -Seconds 3
        $relaunched = @(Get-Process -Name HikariSub -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$dir\HikariSub.exe" })
        Check '/RELAUNCH started HikariSub.exe from the install folder' ($relaunched.Count -ge 1)
        Stop-Installed $dir

        Check 'downgrade: exit 5' ((Run-Setup $OldSetup '/S') -eq 5)
        Check 'version unchanged after the refused downgrade' ((RegValue 'HKCU:\Software\HikariSub\Install' 'Version') -eq $New)

        # Uninstall with user files: they and their folders stay.
        $userFiles = @(Get-ChildItem -Recurse -Force -File -LiteralPath $dir | ForEach-Object { $_.FullName.Substring($dir.Length + 1) } |
            Where-Object { (Manifest-Files $dir) -notcontains $_ -and $_ -notin 'package-kind.txt', 'install-manifest.txt', 'Uninstall HikariSub.exe' })
        Write-Output "  user files before uninstall: $($userFiles -join ', ')"
        Run-Uninstaller $dir '/CURRENTUSER /S' 'HKCU'
        Check-Gone $dir 'HKCU' $programs $desktop $foreign $userFiles
        Remove-Item -Recurse -Force -LiteralPath $dir

        # Fresh silent install with both components, then an uninstall that leaves nothing.
        Check 'silent install /COMPONENTS=assoc,desktop: exit 0' ((Run-Setup $NewSetup '/S /COMPONENTS=assoc,desktop') -eq 0)
        Check-Installed $dir 'HKCU' 'nsis-user' $New $programs 'assoc,desktop'
        $d = if (Test-Path -LiteralPath "$desktop\HikariSub.lnk") { Shortcut-Info "$desktop\HikariSub.lnk" } else { @{} }
        Check 'desktop shortcut created' ($d.Target -eq "$dir\HikariSub.exe" -and $d.Aumid -eq 'io.github.altqx.HikariSub')
        Check 'upgrade keeps recorded components' (((Run-Setup $NewSetup '/S /COMPONENTS=') -eq 0) -and (RegValue 'HKCU:\Software\HikariSub\Install' 'Components') -eq 'assoc,desktop')
        Run-Uninstaller $dir '/CURRENTUSER /S' 'HKCU'
        Check-Gone $dir 'HKCU' $programs $desktop $foreign @()
        Check 'uninstall: HKCU\Software\HikariSub gone' (-not (Test-Path 'HKCU:\Software\HikariSub'))

        # No associations: nothing is registered.
        Check 'silent install /COMPONENTS= (none): exit 0' ((Run-Setup $NewSetup '/S /COMPONENTS=') -eq 0)
        Check 'no ProgIDs without assoc' (-not (Test-Path -LiteralPath "$c\HikariSub.ass") -and (RegValue 'HKCU:\Software\HikariSub\Install' 'Components') -eq '')
        Run-Uninstaller $dir '/CURRENTUSER /S' 'HKCU'
        Check 'uninstall after no-assoc install: folder gone' (-not (Test-Path -LiteralPath $dir))
    } finally {
        Stop-Installed $dir
        if ($null -eq $savedSrt) { Remove-ItemProperty -LiteralPath "$c\.srt" -Name '(default)' -ErrorAction SilentlyContinue }
        else { Set-ItemProperty -LiteralPath "$c\.srt" -Name '(default)' -Value $savedSrt }
    }
} else {
    $root = 'HKLM'
    $dir = Join-Path $env:ProgramW6432 'HikariSub'
    $programs = [Environment]::GetFolderPath('CommonPrograms')
    $desktop = [Environment]::GetFolderPath('CommonDesktopDirectory')
    Write-Output "== all-users phase as $env:USERNAME, $dir"
    Remove-Existing 'HKLM' '/ALLUSERS'
    if (Test-Path -LiteralPath $dir) { Remove-Item -Recurse -Force -LiteralPath $dir }
    $foreign = @{}
    try {
        Check 'fresh all-users install: exit 0' ((Run-Setup $OldSetup '/S /ALLUSERS') -eq 0)
        Check-Installed $dir 'HKLM' 'nsis-machine' $Old $programs 'assoc'
        Check-Associations $dir 'HKLM' $Old $foreign
        Check 'scope conflict: exit 6' ((Run-Setup $NewSetup '/S /CURRENTUSER') -eq 6)
        Check 'silent upgrade (scope from the existing install): exit 0' ((Run-Setup $NewSetup '/S') -eq 0)
        Check-Installed $dir 'HKLM' 'nsis-machine' $New $programs 'assoc'
        Check-Upgraded $dir 'all-users upgrade'
        Check-Associations $dir 'HKLM' $New $foreign
        Check '/UPDATE /ALLUSERS same version: exit 0' ((Run-Setup $NewSetup "/S /UPDATE /ALLUSERS /D=$dir") -eq 0)
        Check 'downgrade: exit 5' ((Run-Setup $OldSetup '/S /ALLUSERS') -eq 5)
        Run-Uninstaller $dir '/ALLUSERS /S' 'HKLM'
        Check-Gone $dir 'HKLM' $programs $desktop $foreign @()
    } finally {
        Stop-Installed $dir
    }
}

Write-Output "== $Phase phase: $script:failures failed"
exit [int]($script:failures -gt 0)
