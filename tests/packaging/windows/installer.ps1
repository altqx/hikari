# Every value comes from the environment:
#   ACTION    run | check-installed | check-removed | user-file | icon-ids | start-app | stop-app | state | dir | open-file | update | cleanup | end-setups
#   SETUP     run: the installer or uninstaller to start
#   ARGS      run: its command line (NSIS rules: /D= last, unquoted)
#   EXPECT    run: the exit code it must return (default 0)
#   SCOPE     user | machine
#   VERSION   check-installed: the version the install must record
#   INSTDIR   the install folder (default: the folder for SCOPE)
#   ORPHANS   check-installed: |-separated payload paths that must be gone
#   KEEP      check-installed, check-removed: |-separated paths that must remain
#   KEEP_USER check-removed: |-separated paths in the user folder (%APPDATA%\HikariSub) that must remain
#   FILE      user-file: a file to write in the user folder
#   LEGACY    check-installed: 1 when the install is legacy's (HikariSub_AVX.exe allowed)
#   CLOSE_APP update: 1 closes the running app so the waiting installer goes on
# The exit code is the number of failed checks (capped at 100).
$ErrorActionPreference = 'Stop'
$failures = [System.Collections.Generic.List[string]]::new()
function Fail([string]$what) { $failures.Add($what); Write-Output "FAIL $what" }
function Pass([string]$what) { Write-Output "ok   $what" }
function Check([bool]$cond, [string]$what) { if ($cond) { Pass $what } else { Fail $what } }

$scope = if ($env:SCOPE) { $env:SCOPE } else { 'user' }
$hive = if ($scope -eq 'machine') { 'HKLM:' } else { 'HKCU:' }
$defaultDir = if ($scope -eq 'machine') { Join-Path $env:ProgramW6432 'HikariSub' } else { Join-Path $env:LOCALAPPDATA 'Programs\HikariSub' }
$instDir = if ($env:INSTDIR) { $env:INSTDIR.TrimEnd('\') } else { $defaultDir }
$exe = Join-Path $instDir 'HikariSub.exe'
$uninstallKey = "$hive\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub"
$userFolder = Join-Path $env:APPDATA 'HikariSub'
$installKey = "$hive\Software\HikariSub\Install"
$classes = "$hive\Software\Classes"
$programs = if ($scope -eq 'machine') { [Environment]::GetFolderPath('CommonPrograms') } else { [Environment]::GetFolderPath('Programs') }
$desktop = if ($scope -eq 'machine') { [Environment]::GetFolderPath('CommonDesktopDirectory') } else { [Environment]::GetFolderPath('DesktopDirectory') }
$types = [ordered]@{ '.ass' = 109; '.ssa' = 110; '.srt' = 111; '.sub' = 112 }

function Value($path, $name) {
    try { (Get-ItemProperty -LiteralPath $path -Name $name -ErrorAction Stop).$name } catch { $null }
}

function Shortcut-Target($lnk) {
    $shell = New-Object -ComObject WScript.Shell
    $shell.CreateShortcut($lnk).TargetPath
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HsRes {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll")] static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
    // RT_GROUP_ICON (14) with the numeric id; LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE.
    public static bool HasIconGroup(string path, int id) {
        IntPtr m = LoadLibraryExW(path, IntPtr.Zero, 0x22);
        if (m == IntPtr.Zero) return false;
        try { return FindResourceW(m, (IntPtr)id, (IntPtr)14) != IntPtr.Zero; } finally { FreeLibrary(m); }
    }
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("advapi32.dll")] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll")] static extern bool GetTokenInformation(IntPtr token, int cls, out int info, int len, out int retLen);
    // TokenElevation (20) of a process, through PROCESS_QUERY_LIMITED_INFORMATION and TOKEN_QUERY.
    public static bool IsElevated(int pid) {
        IntPtr p = OpenProcess(0x1000, false, pid);
        if (p == IntPtr.Zero) return false;
        try {
            IntPtr t;
            if (!OpenProcessToken(p, 0x8, out t)) return false;
            try { int e, n; return GetTokenInformation(t, 20, out e, 4, out n) && e != 0; } finally { CloseHandle(t); }
        } finally { CloseHandle(p); }
    }
}
'@

switch ($env:ACTION) {
    'run' {
        $expect = if ($env:EXPECT) { [int]$env:EXPECT } else { 0 }
        Write-Output "run: `"$env:SETUP`" $env:ARGS"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = if ($env:ARGS) { Start-Process -FilePath $env:SETUP -ArgumentList $env:ARGS -PassThru } else { Start-Process -FilePath $env:SETUP -PassThru }
        $null = $p.Handle
        if (-not $p.WaitForExit(600000)) { Fail 'installer still running after 10 minutes'; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; break }
        # The NSIS uninstaller runs a copy of itself from %TEMP%\~nsu*.tmp and returns at once.
        $code = $p.ExitCode
        $copy = $null
        $deadline = (Get-Date).AddSeconds($(if ($env:SETUP -like '*Uninstall*') { 10 } else { 0 }))
        while (-not $copy -and (Get-Date) -lt $deadline) {
            $copy = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$env:TEMP\~nsu*" } | Select-Object -First 1
            if (-not $copy) { Start-Sleep -Milliseconds 100 }
        }
        if ($copy) {
            $null = $copy.Handle
            if (-not $copy.WaitForExit(600000)) { Fail 'uninstaller copy still running after 10 minutes'; Stop-Process -Id $copy.Id -Force -ErrorAction SilentlyContinue; break }
            Write-Output "uninstaller copy $($copy.Path) exit $($copy.ExitCode)"
            $code = $copy.ExitCode
        }
        Write-Output "exit $code after $([math]::Round($sw.Elapsed.TotalSeconds, 1)) s"
        Check ($code -eq $expect) "exit code $code, expected $expect"
    }
    'check-installed' {
        Check (Test-Path -LiteralPath $exe) "$exe exists"
        $kind = if (Test-Path -LiteralPath "$instDir\package-kind.txt") { (Get-Content -Raw -LiteralPath "$instDir\package-kind.txt").Trim() } else { $null }
        Check ($kind -eq "nsis-$scope") "package-kind.txt is nsis-$scope (found '$kind')"
        $manifest = "$instDir\install-manifest.txt"
        if (Test-Path -LiteralPath $manifest) {
            $listed = @(Get-Content -LiteralPath $manifest | Where-Object { $_ })
            $missing = @($listed | Where-Object { -not (Test-Path -LiteralPath (Join-Path $instDir $_)) })
            Check ($missing.Count -eq 0) "every one of the $($listed.Count) manifest files exists$(if ($missing) { ': missing ' + ($missing | Select-Object -First 5) -join ', ' })"
            $sorted = @($listed | Sort-Object)
            Check (-not ($listed | Where-Object { $_ -match '^(package-kind|install-manifest)\.txt$|^Uninstall HikariSub\.exe$' })) 'manifest lists no installer-written file'
        } else { Fail 'install-manifest.txt exists' }
        Check (Test-Path -LiteralPath "$instDir\Uninstall HikariSub.exe") 'uninstaller exists'
        if (-not $env:LEGACY) { Check (-not (Test-Path -LiteralPath "$instDir\HikariSub_AVX.exe")) 'no HikariSub_AVX.exe (legacy orphan removed)' }
        foreach ($o in ($env:ORPHANS -split '\|' | Where-Object { $_ })) { Check (-not (Test-Path -LiteralPath (Join-Path $instDir $o))) "orphan $o removed" }
        foreach ($k in ($env:KEEP -split '\|' | Where-Object { $_ })) { Check (Test-Path -LiteralPath (Join-Path $instDir $k)) "kept $k" }

        Check (Test-Path $uninstallKey) "$uninstallKey exists"
        Check ((Value $uninstallKey DisplayName) -eq 'HikariSub') 'DisplayName HikariSub'
        Check ((Value $uninstallKey DisplayVersion) -eq $env:VERSION) "DisplayVersion $env:VERSION (found '$(Value $uninstallKey DisplayVersion)')"
        Check ((Value $uninstallKey Publisher) -eq 'altqx') 'Publisher altqx'
        Check ((Value $uninstallKey InstallLocation) -eq $instDir) "InstallLocation $instDir (found '$(Value $uninstallKey InstallLocation)')"
        Check ((Value $uninstallKey DisplayIcon) -eq "`"$exe`",0") 'DisplayIcon'
        $flag = if ($scope -eq 'machine') { '/ALLUSERS' } else { '/CURRENTUSER' }
        $us = "`"$instDir\Uninstall HikariSub.exe`" $flag"
        Check ((Value $uninstallKey UninstallString) -eq $us) "UninstallString $us (found '$(Value $uninstallKey UninstallString)')"
        Check ((Value $uninstallKey QuietUninstallString) -eq "$us /S") 'QuietUninstallString'
        Check ((Value $uninstallKey InstallScope) -eq $scope) "InstallScope $scope"
        Check ((Value $uninstallKey NoModify) -eq 1 -and (Value $uninstallKey NoRepair) -eq 1) 'NoModify, NoRepair'
        Check ([int](Value $uninstallKey EstimatedSize) -gt 0) 'EstimatedSize'
        $other = if ($scope -eq 'machine') { 'HKCU:' } else { 'HKLM:' }
        Check (-not (Test-Path "$other\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub")) "no Uninstall key under $other"

        Check ((Value $installKey Version) -eq $env:VERSION) 'Install\Version'
        Check ((Value $installKey InstallScope) -eq $scope) 'Install\InstallScope'
        Check ((Value $installKey InstallLocation) -eq $instDir) 'Install\InstallLocation'
        $components = "$(Value $installKey Components)"
        Write-Output "     components: '$components'"

        $appPaths = "$hive\Software\Microsoft\Windows\CurrentVersion\App Paths\HikariSub.exe"
        Check ((Value $appPaths '(default)') -eq $exe) 'App Paths default'
        Check ((Value $appPaths Path) -eq $instDir) 'App Paths Path'

        $lnk = Join-Path $programs 'HikariSub.lnk'
        Check ((Test-Path -LiteralPath $lnk) -and (Shortcut-Target $lnk) -eq $exe) "Start menu shortcut $lnk -> $exe"
        $wantDesktop = ($components -split ',') -contains 'desktop'
        Check ((Test-Path -LiteralPath (Join-Path $desktop 'HikariSub.lnk')) -eq $wantDesktop) "desktop shortcut present = $wantDesktop"

        if (($components -split ',') -contains 'assoc') {
            foreach ($ext in $types.Keys) {
                $progid = "HikariSub$ext"
                Check ((Value "$classes\$progid\shell\open\command" '(default)') -eq "`"$exe`" `"%1`"") "$progid open command"
                Check ((Value "$classes\$progid\DefaultIcon" '(default)') -eq "`"$exe`",-$($types[$ext])") "$progid DefaultIcon -$($types[$ext])"
                Check ($null -ne (Get-ItemProperty -LiteralPath "$classes\$ext\OpenWithProgids" -ErrorAction SilentlyContinue).$progid) "$ext OpenWithProgids has $progid"
                Check ($null -ne (Get-ItemProperty -LiteralPath "$classes\Applications\HikariSub.exe\SupportedTypes" -ErrorAction SilentlyContinue).$ext) "SupportedTypes has $ext"
                Check ((Value "$hive\Software\HikariSub\Capabilities\FileAssociations" $ext) -eq $progid) "Capabilities $ext"
            }
            Check ((Value "$hive\Software\RegisteredApplications" HikariSub) -eq 'Software\HikariSub\Capabilities') 'RegisteredApplications'
            Check ($null -eq (Get-ItemProperty -LiteralPath "$classes\.txt\OpenWithProgids" -ErrorAction SilentlyContinue).'HikariSub.txt') '.txt untouched'
        } else {
            Check (-not (Test-Path "$classes\HikariSub.ass")) 'no association without the component'
        }
    }
    'check-removed' {
        Check (-not (Test-Path -LiteralPath $exe)) "$exe removed"
        Check (-not (Test-Path -LiteralPath "$instDir\Uninstall HikariSub.exe")) 'uninstaller removed'
        Check (-not (Test-Path -LiteralPath "$instDir\package-kind.txt")) 'package-kind.txt removed'
        Check (-not (Test-Path -LiteralPath "$instDir\install-manifest.txt")) 'install-manifest.txt removed'
        $keep = @($env:KEEP -split '\|' | Where-Object { $_ })
        foreach ($k in $keep) { Check (Test-Path -LiteralPath (Join-Path $instDir $k)) "kept $k" }
        if (-not $keep) { Check (-not (Test-Path -LiteralPath $instDir)) "$instDir removed" }
        Check (-not (Test-Path $uninstallKey)) 'Uninstall key removed'
        Check (-not (Test-Path $installKey)) 'Install key removed'
        Check (-not (Test-Path "$hive\Software\HikariSub\Capabilities")) 'Capabilities removed'
        Check ($null -eq (Value "$hive\Software\RegisteredApplications" HikariSub)) 'RegisteredApplications value removed'
        Check (-not (Test-Path "$hive\Software\Microsoft\Windows\CurrentVersion\App Paths\HikariSub.exe")) 'App Paths removed'
        Check (-not (Test-Path -LiteralPath (Join-Path $programs 'HikariSub.lnk'))) 'Start menu shortcut removed'
        Check (-not (Test-Path -LiteralPath (Join-Path $desktop 'HikariSub.lnk'))) 'desktop shortcut removed'
        foreach ($ext in $types.Keys) {
            Check (-not (Test-Path "$classes\HikariSub$ext")) "HikariSub$ext removed"
            Check ($null -eq (Get-ItemProperty -LiteralPath "$classes\$ext\OpenWithProgids" -ErrorAction SilentlyContinue)."HikariSub$ext") "$ext OpenWithProgids entry removed"
            Check ((Value "$classes\$ext" '(default)') -ne "HikariSub$ext") "$ext default no longer HikariSub$ext"
        }
        Check (-not (Test-Path "$classes\Applications\HikariSub.exe")) 'Applications\HikariSub.exe removed'
        foreach ($k in @($env:KEEP_USER -split '\|' | Where-Object { $_ })) {
            Check (Test-Path -LiteralPath (Join-Path $userFolder $k)) "user folder kept $k"
        }
    }
    'user-file' {
        New-Item -ItemType Directory -Force -Path $userFolder | Out-Null
        Set-Content -LiteralPath (Join-Path $userFolder $env:FILE) -Value 'kept by the uninstaller'
        Check (Test-Path -LiteralPath (Join-Path $userFolder $env:FILE)) "wrote $env:FILE in the user folder"
    }
    'icon-ids' {
        $target = if ($env:EXE) { $env:EXE } else { $exe }
        foreach ($id in 1, 2, 109, 110, 111, 112, 113) { Check ([HsRes]::HasIconGroup($target, $id)) "$target has icon group $id" }
        $info = (Get-Item -LiteralPath $target).VersionInfo
        Write-Output "     ProductName '$($info.ProductName)', ProductVersion '$($info.ProductVersion)', OriginalFilename '$($info.OriginalFilename)'"
        Check ($info.ProductName -eq 'HikariSub') 'version resource names HikariSub'
    }
    'start-app' {
        $p = Start-Process -FilePath $exe -PassThru
        Start-Sleep -Seconds 5
        Check (-not $p.HasExited) "HikariSub.exe started (pid $($p.Id))"
        Set-Content -LiteralPath "$env:TEMP\hs-app.pid" -Value $p.Id
        Write-Output "pid $($p.Id)"
    }
    'stop-app' {
        $procs = @(Get-Process -Name HikariSub -ErrorAction SilentlyContinue)
        foreach ($p in $procs) { $null = $p.CloseMainWindow() }
        foreach ($p in $procs) { if (-not $p.WaitForExit(15000)) { $p.Kill() } }
        Write-Output "stopped $($procs.Count)"
    }
    'state' {
        Get-Process -Name HikariSub -ErrorAction SilentlyContinue | ForEach-Object { "running pid $($_.Id) $($_.Path) started $($_.StartTime)" }
        foreach ($h in 'HKCU:', 'HKLM:') {
            $k = "$h\Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub"
            if (Test-Path $k) { "$k = $(Value $k DisplayVersion) at $(Value $k InstallLocation)" }
        }
        Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'HikariSub\Updates') -Filter 'last-result-*.json' -ErrorAction SilentlyContinue |
            ForEach-Object { "$($_.Name): $(Get-Content -Raw -LiteralPath $_.FullName)" }
    }
    'open-file' {
        $doc = Join-Path $env:TEMP 'hs-assoc-test.ass'
        Set-Content -LiteralPath $doc -Value "[Script Info]`r`nScriptType: v4.00+`r`n"
        Write-Output "     .ass default: '$(Value "$classes\.ass" '(default)')', UserChoice: '$(Value 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.ass\UserChoice' ProgId)'"
        Start-Process -FilePath $doc
        $found = $null
        $deadline = (Get-Date).AddSeconds(20)
        while (-not $found -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 500
            $found = Get-CimInstance Win32_Process -Filter "Name = 'HikariSub.exe'" | Where-Object { $_.CommandLine -like "*hs-assoc-test.ass*" } | Select-Object -First 1
        }
        Check ($null -ne $found) 'opening hs-assoc-test.ass started HikariSub.exe with it'
        if ($found) {
            Write-Output "     $($found.CommandLine)"
            Check ($found.ExecutablePath -eq $exe) "started $($found.ExecutablePath)"
            Stop-Process -Id $found.ProcessId -Force
        }
    }
    'update' {
        $expect = if ($env:EXPECT) { [int]$env:EXPECT } else { 0 }
        $app = Get-Process -Name HikariSub -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $app) { Fail 'no running HikariSub to update'; break }
        $copy = if ("$env:ARGS" -match '/ALLUSERS') { 'nsis-machine' } else { 'nsis-user' }
        $result = Join-Path $env:LOCALAPPDATA "HikariSub\Updates\last-result-$copy.json"
        Remove-Item -LiteralPath $result -ErrorAction SilentlyContinue
        $a = "/S /UPDATE /WAITPID=$($app.Id) $env:ARGS".Trim()
        Write-Output "run: `"$env:SETUP`" $a"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process -FilePath $env:SETUP -ArgumentList $a -PassThru
        $null = $p.Handle
        Start-Sleep -Seconds 5
        Check (-not $p.HasExited) 'the installer waits while the app runs'
        Check ((Value $uninstallKey DisplayVersion) -ne $env:VERSION) 'nothing installed yet'
        if ($env:CLOSE_APP -eq '1') {
            $null = $app.CloseMainWindow()
            if (-not $app.WaitForExit(15000)) { $app.Kill() }
            Write-Output "app $($app.Id) exited after $([math]::Round($sw.Elapsed.TotalSeconds, 1)) s"
        }
        if (-not $p.WaitForExit(300000)) { Fail 'installer still running after 5 minutes'; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; break }
        Write-Output "exit $($p.ExitCode) after $([math]::Round($sw.Elapsed.TotalSeconds, 1)) s"
        Check ($p.ExitCode -eq $expect) "exit code $($p.ExitCode), expected $expect"
        if ($expect -eq 0) {
            Check ((Value $uninstallKey DisplayVersion) -eq $env:VERSION) "DisplayVersion $env:VERSION"
            Check (-not (Test-Path -LiteralPath $result)) 'no last-result.json'
        } else {
            Check (Test-Path -LiteralPath $result) 'last-result.json written'
            if (Test-Path -LiteralPath $result) { Write-Output "     $(Get-Content -Raw -LiteralPath $result)" }
            Remove-Item -LiteralPath $result -ErrorAction SilentlyContinue
        }
        if ($env:ARGS -match '/RELAUNCH') {
            $new = $null
            $deadline = (Get-Date).AddSeconds(20)
            while (-not $new -and (Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 500
                $new = Get-Process -Name HikariSub -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $app.Id } | Select-Object -First 1
            }
            Check ($null -ne $new) 'the installer relaunched HikariSub'
            if ($new) {
                Check ($new.Path -eq $exe) "relaunched $($new.Path)"
                $owner = (Get-CimInstance Win32_Process -Filter "ProcessId = $($new.Id)" | Invoke-CimMethod -MethodName GetOwner)
                Write-Output "     relaunched pid $($new.Id) as $($owner.Domain)\$($owner.User), elevated: $([HsRes]::IsElevated($new.Id))"
                Check (-not [HsRes]::IsElevated($new.Id)) 'the relaunched app is not elevated'
                $null = $new.CloseMainWindow()
                if (-not $new.WaitForExit(15000)) { $new.Kill() }
            }
        }
    }
    'cleanup' {
        if (Test-Path $uninstallKey) { Fail "an install is registered at $uninstallKey"; break }
        Remove-Item -Recurse -Force -LiteralPath $instDir -ErrorAction SilentlyContinue
        Remove-Item -Force -LiteralPath (Join-Path $env:TEMP 'hs-assoc-test.ass') -ErrorAction SilentlyContinue
        Check (-not (Test-Path -LiteralPath $instDir)) "$instDir removed"
    }
    'dir' {
        if (Test-Path -LiteralPath $instDir) {
            Get-ChildItem -LiteralPath $instDir | ForEach-Object { "$($_.Mode) $($_.Name)" }
            Write-Output "files: $((Get-ChildItem -Recurse -File -LiteralPath $instDir).Count)"
        } else { Write-Output "$instDir does not exist" }
    }
    'end-setups' {
        $out = & (Join-Path $PSScriptRoot '..\..\..\tools\winix\end-setups.ps1')
        $code = $LASTEXITCODE
        $out | ForEach-Object { Write-Output "     $_" }
        Check ($code -eq 0) 'no setup or uninstaller left running'
    }
    default { Fail "unknown ACTION '$env:ACTION'" }
}
Write-Output "== $($failures.Count) failure(s)"
exit [math]::Min($failures.Count, 100)
