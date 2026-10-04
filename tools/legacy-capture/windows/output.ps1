# S3 on Windows (winix task legacy-win-output): prints what drive_windows.py
# reads back after a run: the probe's JSON lines, the Config directory the
# legacy app left and its Hotkeys.txt, each after a marker line.
$ErrorActionPreference = 'Stop'
$root = Join-Path $PWD 'out\legacy-win'
$app = Join-Path $root 'probe\HikariSub_x64'
$output = Join-Path $root 'probe\capture.jsonl'
Write-Output "== processes"
Get-Process HikariSub -ErrorAction SilentlyContinue | ForEach-Object { Write-Output "$($_.Id) responding=$($_.Responding) session=$($_.SessionId) $($_.MainWindowTitle)" }
Write-Output "== config"
if (Test-Path "$app\Config") { Get-ChildItem "$app\Config" | ForEach-Object { Write-Output $_.Name } }
Write-Output "== hotkeys"
if (Test-Path "$app\Config\Hotkeys.txt") { Get-Content -Raw -Encoding UTF8 "$app\Config\Hotkeys.txt" | Write-Output }
Write-Output "== capture"
if (Test-Path $output) {
    # One JSON line per macro run; written as base64 so no line is wrapped or re-encoded.
    Write-Output ([Convert]::ToBase64String([IO.File]::ReadAllBytes($output)))
}
Write-Output "== end"
