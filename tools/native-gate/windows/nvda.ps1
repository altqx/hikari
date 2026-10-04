# D1 native gate on Windows: NVDA for the `nvda` step (winix tasks nvda-setup
# and nvda-output). Approved by the user for the Winix VM guest.
#   -Action setup   download the pinned NVDA release, check its published
#                   SHA-256, create a portable copy under out\native-gate-win\nvda
#                   (nothing is installed) and write its configuration: the
#                   "No speech" synthesizer (silence), no sounds, no welcome
#                   dialog, no update checks. NVDA_LAYOUT line.
#   -Action output  quit NVDA (nvda.exe -q, then end what is left of the
#                   portable copy), copy the log named by GATE_NVDA_LOG to
#                   nvda\nvda-last.log (the task's artifact) and print its
#                   speech lines.
# gate_windows.py starts the portable copy itself through `winix ui launch`
# (a winix task ends every process it started).
param([Parameter(Mandatory)][ValidateSet('setup', 'output')][string]$Action)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
# NVDA 2026.2: https://www.nvaccess.org/post/nvda-2026-2/ publishes this
# SHA-256 (the update API's launcherHash, SHA-1, is a5b01592f4e2355432848d3ad82f5bd214f25947).
$version = '2026.2'
$sha256 = 'f3f8d29974a88d687b3c4809be192219ec579c5bdabcda5aaf53635288bca824'
$url = "https://download.nvaccess.org/releases/$version/nvda_$version.exe"
$dir = Join-Path $PWD.Path 'out\native-gate-win\nvda'
$launcher = Join-Path $dir "nvda_$version.exe"
$portable = Join-Path $dir "portable-$version"
$config = Join-Path $dir 'config'
New-Item -ItemType Directory -Force $dir, $config | Out-Null

function PortableProcesses {
    @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -and $_.Path.StartsWith($portable, [StringComparison]::OrdinalIgnoreCase) })
}

switch ($Action) {
    'setup' {
        $have = (Test-Path $launcher) -and ((Get-FileHash $launcher -Algorithm SHA256).Hash.ToLower() -eq $sha256)
        if (-not $have) { Invoke-WebRequest $url -OutFile $launcher -UseBasicParsing }
        $hash = (Get-FileHash $launcher -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $sha256) { Remove-Item $launcher; throw "checksum mismatch: $hash, NV Access publishes $sha256" }
        Write-Output "launcher $launcher sha256 $hash (published)"
        if (-not (Test-Path (Join-Path $portable 'nvda.exe'))) {
            $p = Start-Process -FilePath $launcher -ArgumentList '--create-portable-silent', "--portable-path=`"$portable`"" -PassThru
            $null = $p.Handle
            if (-not $p.WaitForExit(600000)) { $p.Kill(); throw 'the NVDA launcher did not finish in 10 minutes' }
            foreach ($i in 1..120) {
                if ((Test-Path (Join-Path $portable 'nvda.exe')) -and -not (Get-Process -Name 'nvda*' -ErrorAction SilentlyContinue)) { break }
                Start-Sleep -Seconds 1
            }
            if (-not (Test-Path (Join-Path $portable 'nvda.exe'))) { throw "no portable copy at $portable (launcher exit $($p.ExitCode))" }
        }
        # NVDA's configuration (ConfigObj). Speech goes to the "No speech"
        # synthesizer; the io log level still records every "Speaking [...]".
        $ini = @(
            '[general]', "`tlanguage = Windows", "`tshowWelcomeDialogAtStartup = False", "`tplayStartAndExitSounds = False",
            "`tsaveConfigurationOnExit = False", "`taskToExit = False",
            '[speech]', "`tsynth = silence",
            '[braille]', "`tdisplay = noBraille",
            '[update]', "`tautoCheck = False", "`tstartupNotification = False", "`tallowUsageStats = False", "`taskedAllowUsageStats = True"
        ) -join "`r`n"
        [IO.File]::WriteAllText((Join-Path $config 'nvda.ini'), $ini + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
        $layout = [ordered]@{ version = $version; sha256 = $sha256; exe = Join-Path $portable 'nvda.exe'; config = $config; dir = $dir }
        Write-Output ('NVDA_LAYOUT ' + ($layout | ConvertTo-Json -Compress))
    }
    'output' {
        $exe = Join-Path $portable 'nvda.exe'
        Write-Output '== quit'
        if ((PortableProcesses).Count -and (Test-Path $exe)) {
            $q = Start-Process -FilePath $exe -ArgumentList '-q' -PassThru
            $null = $q.WaitForExit(30000)
            foreach ($i in 1..30) { if (-not (PortableProcesses).Count) { break }; Start-Sleep -Seconds 1 }
        }
        $left = PortableProcesses
        if ($left.Count) { $left | Stop-Process -Force; Write-Output "ended $($left.Count) NVDA process(es) that did not quit" }
        else { Write-Output 'NVDA quit' }
        $log = $env:GATE_NVDA_LOG
        $last = Join-Path $dir 'nvda-last.log'
        Remove-Item $last -ErrorAction SilentlyContinue
        if (-not $log -or -not (Test-Path $log)) { Write-Output "== no log at '$log'"; Write-Output '== end'; exit 0 }
        Start-Sleep -Seconds 1
        Copy-Item $log $last
        $lines = Get-Content -Encoding UTF8 $last
        Write-Output "== log $log $((Get-Item $last).Length) bytes, $($lines.Count) lines"
        Write-Output '== synth'
        $lines | Select-String -Pattern 'synthDriver|Loaded synth|silence|No speech' | Select-Object -First 20 | ForEach-Object { $_.Line }
        Write-Output '== errors'
        $lines | Select-String -Pattern '^ERROR' -Context 0, 2 | Select-Object -First 20 | ForEach-Object { $_.ToString() }
        Write-Output '== speech'
        $lines | Where-Object { $_ -like 'Speaking *' }
        Write-Output '== end'
    }
}
