# S4 on Windows: Document scripts and "Run the last loaded script" in the
# legacy release (winix tasks legacy-win-s4-setup, -touch and -output; driven by
# drive_windows.py --document-scripts). Run legacy-win-setup first: it leaves
# the checked release zip in out/legacy-win.
#   -Action setup   one fresh copy per case of plan.json's document_scripts
#                   section in out/legacy-win/s4/<case>: Autoload emptied, the
#                   scripts (record.lua.in before each body, as
#                   document_scripts_capture.py writes them) in s4\abs (the
#                   absolute folder) and beside the exe in s4 (relative paths
#                   resolve against the working directory, the exe's folder
#                   for a desktop launch), capture-probe.cfg beside each, the
#                   document, and GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT bound to
#                   the plan's hotkey in Config\Hotkeys.txt. The last line is
#                   the layout as JSON (S4_LAYOUT).
#   -Action touch   appends a comment to $env:S4_SCRIPT (default last) in every case's two
#                   folders and moves its time a minute on.
#   -Action output  each case's capture lines (base64) after "== <case>".
param([ValidateSet('setup', 'touch', 'output')][string]$Action = 'setup')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$root = Join-Path $PWD 'out\legacy-win'
$s4 = Join-Path $root 's4'
$plan = Get-Content -Raw -Encoding UTF8 (Join-Path $PWD 'tools\legacy-capture\plan.json') | ConvertFrom-Json
$spec = $plan.document_scripts

function Folders($case) {
    $dir = Join-Path $s4 $case.id
    @((Join-Path $dir 'abs'), (Join-Path $dir 'HikariSub_x64\s4'))
}

if ($Action -eq 'touch') {
    if (-not $env:S4_SCRIPT) { $env:S4_SCRIPT = 'last' }
    foreach ($case in $spec.cases) {
        foreach ($folder in Folders $case) {
            $path = Join-Path $folder "$($env:S4_SCRIPT).lua"
            if (-not (Test-Path $path)) { continue }
            [IO.File]::AppendAllText($path, "-- changed`n", $utf8)
            (Get-Item $path).LastWriteTime = (Get-Date).AddMinutes(1)
        }
    }
    Write-Output "touched $($env:S4_SCRIPT)"
    exit 0
}

if ($Action -eq 'output') {
    Write-Output "== processes"
    Get-Process HikariSub -ErrorAction SilentlyContinue | ForEach-Object { Write-Output "$($_.Id) responding=$($_.Responding) $($_.MainWindowTitle)" }
    foreach ($case in $spec.cases) {
        Write-Output "== $($case.id)"
        $out = Join-Path $s4 "$($case.id)\capture.jsonl"
        if (Test-Path $out) { Write-Output ([Convert]::ToBase64String([IO.File]::ReadAllBytes($out))) }
    }
    Write-Output "== end"
    exit 0
}

$zip = Get-ChildItem $root -Filter 'HikariSub-*-windows-x64.zip' | Select-Object -First 1
if (-not $zip) { throw 'run legacy-win-setup first (no release zip in out/legacy-win)' }
Get-Process HikariSub -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
if (Test-Path $s4) { Remove-Item -Recurse -Force $s4 }
$scriptsDir = Join-Path $PWD ('tools\legacy-capture\' + ($spec.scripts_dir -replace '/', '\'))
$head = [IO.File]::ReadAllText((Join-Path $scriptsDir 'record.lua.in'), $utf8)
$layout = [ordered]@{ zip = $zip.Name; cases = [ordered]@{} }
foreach ($case in $spec.cases) {
    $dir = Join-Path $s4 $case.id
    Expand-Archive $zip.FullName -DestinationPath $dir
    $app = Join-Path $dir 'HikariSub_x64'
    Get-ChildItem (Join-Path $app 'Automation\automation\Autoload') | Remove-Item -Recurse -Force
    $out = Join-Path $dir 'capture.jsonl'
    [IO.File]::WriteAllText($out, '', $utf8)
    foreach ($folder in Folders $case) {
        New-Item -ItemType Directory -Force $folder | Out-Null
        foreach ($name in $spec.scripts) {
            $body = [IO.File]::ReadAllText((Join-Path $scriptsDir "$name.lua"), $utf8)
            $text = $head.Replace('local tag = TAG', "local tag = `"$name`"") + "`n" + $body
            [IO.File]::WriteAllText((Join-Path $folder "$name.lua"), $text, $utf8)
        }
        [IO.File]::WriteAllText((Join-Path $folder 'capture-probe.cfg'), "HIKARI_CAPTURE_OUT=$out`r`n", $utf8)
    }
    $config = Join-Path $app 'Config'
    New-Item -ItemType Directory -Force $config | Out-Null
    $lines = @('[HikariSub 0.0.1.9999]') + (0..10 | ForEach-Object { "Script C:\nonexistent\none.lua-$_=Ctrl-Alt-F$($_ + 1)" })
    $lines += "GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT G=$($spec.hotkey)"
    [IO.File]::WriteAllText((Join-Path $config 'Hotkeys.txt'), (($lines -join "`r`n") + "`r`n"), $utf8)
    $info = "[Script Info]`r`nTitle: s4`r`nScriptType: v4.00+`r`n"
    if ($null -ne $case.document_scripts) {
        $abs = (Folders $case)[0]
        $info += "Automation Scripts: " + ($case.document_scripts.Replace('{abs}', $abs) -replace '/', '\') + "`r`n"
    }
    $doc = Join-Path $dir 's4.ass'
    [IO.File]::WriteAllText($doc, $info + "`r`n[V4+ Styles]`r`n" + $spec.styles + "`r`n`r`n[Events]`r`n" + $spec.events, $utf8)
    (Get-Item $doc).LastWriteTime = (Get-Date).AddMinutes(-2)
    $layout.cases[$case.id] = [ordered]@{ exe = Join-Path $app 'HikariSub.exe'; doc = $doc }
}
Write-Output ("S4_LAYOUT " + ($layout | ConvertTo-Json -Compress -Depth 5))
