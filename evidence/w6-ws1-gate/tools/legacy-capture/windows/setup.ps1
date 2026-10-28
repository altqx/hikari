# S3 on Windows: prepares two fresh copies of the legacy release for
# drive_windows.py (winix task legacy-win-setup). The release zip is the
# public GitHub release built from the legacy baseline; it is downloaded once
# into out/legacy-win, checked against the release's SHA256SUMS and extracted
# twice:
#   bundled\HikariSub_x64  as shipped (startup with the bundled Autoload)
#   probe\HikariSub_x64    Autoload emptied into probe\corpus, capture-probe.lua
#                          and its .cfg in Autoload, the probe's macros bound to
#                          Ctrl+Shift+F1..F12 in Config\Hotkeys.txt
# The legacy app keeps its configuration beside its executable, so nothing
# outside out/legacy-win is touched. The last line is the layout as JSON.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$tag = 'v0.0.1-rc.1'
$zipName = 'HikariSub-0.0.1-rc.1-windows-x64.zip'
$base = "https://github.com/altqx/hikari/releases/download/$tag"
$root = Join-Path $PWD 'out\legacy-win'
New-Item -ItemType Directory -Force $root | Out-Null

$sums = Join-Path $root 'SHA256SUMS'
Invoke-WebRequest "$base/SHA256SUMS" -OutFile $sums -UseBasicParsing
$expected = (Get-Content $sums | Where-Object { $_ -match "\s\*?$([regex]::Escape($zipName))$" } | ForEach-Object { ($_ -split '\s+')[0] }) | Select-Object -First 1
if (-not $expected) { throw "$zipName is not in SHA256SUMS" }
$zip = Join-Path $root $zipName
if (-not (Test-Path $zip) -or (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower() -ne $expected.ToLower()) {
    Invoke-WebRequest "$base/$zipName" -OutFile $zip -UseBasicParsing
}
$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if ($zipHash -ne $expected.ToLower()) { throw "checksum mismatch: $zipHash, SHA256SUMS says $expected" }

Get-Process HikariSub -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
foreach ($part in 'bundled', 'probe') {
    $dir = Join-Path $root $part
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Expand-Archive $zip -DestinationPath $dir
}

# The scratch document drive.py opens (drive_windows.py passes it as the
# launch argument when the desktop launch takes one).
$docText = "[Script Info]`nScriptType: v4.00+`n`n[Events]`nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text`nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,probe`n"
foreach ($part in 'bundled', 'probe') {
    New-Item -ItemType Directory -Force (Join-Path $root "$part\doc") | Out-Null
    [IO.File]::WriteAllText((Join-Path $root "$part\doc\probe.ass"), $docText, (New-Object System.Text.UTF8Encoding($false)))
}

$app = Join-Path $root 'probe\HikariSub_x64'
$autoload = Join-Path $app 'Automation\automation\Autoload'
$corpusDir = Join-Path $root 'probe\corpus'
New-Item -ItemType Directory -Force $corpusDir | Out-Null
# The Autoload files (not its subfolders) become the corpus, as on Linux.
$files = Get-ChildItem $autoload -File
$names = [string[]]($files | ForEach-Object { $_.Name })
[Array]::Sort($names, [StringComparer]::Ordinal)
foreach ($f in $files) { Move-Item $f.FullName (Join-Path $corpusDir $f.Name) }
$corpusList = Join-Path $root 'probe\corpus.txt'
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($corpusList, (($names | ForEach-Object { (Join-Path $corpusDir $_) + "`n" }) -join ''), $utf8)

$probeSource = Join-Path $PWD 'tools\legacy-capture\automation\capture-probe.lua'
$probe = Join-Path $autoload 'capture-probe.lua'
Copy-Item $probeSource $probe
$output = Join-Path $root 'probe\capture.jsonl'
[IO.File]::WriteAllText($output, '', $utf8)
# A desktop launch cannot set the probe's environment; it reads this instead.
$cfg = "HIKARI_CAPTURE_OUT=$output`r`nHIKARI_CAPTURE_CORPUS=$corpusList`r`nHIKARI_CAPTURE_AT_LOAD=1`r`n"
[IO.File]::WriteAllText((Join-Path $autoload 'capture-probe.cfg'), $cfg, $utf8)

# Legacy's script hotkeys name the script by the full path the host loaded it
# under ("Script <path>-<macro>=<keys>"); the header's build number keeps
# LoadHkeys from converting or replacing the file (see drive.py).
$config = Join-Path $app 'Config'
New-Item -ItemType Directory -Force $config | Out-Null
$lines = @('[HikariSub 0.0.1.9999]') + (0..11 | ForEach-Object { "Script $probe-$_=Ctrl-Shift-F$($_ + 1)" })
[IO.File]::WriteAllText((Join-Path $config 'Hotkeys.txt'), (($lines -join "`r`n") + "`r`n"), $utf8)

$layout = [ordered]@{
    zip = $zipName; zip_sha256 = $zipHash; tag = $tag
    bundled_exe = Join-Path $root 'bundled\HikariSub_x64\HikariSub.exe'
    bundled_autoload = [string[]](Get-ChildItem (Join-Path $root 'bundled\HikariSub_x64\Automation\automation\Autoload') -File | ForEach-Object { $_.Name } | Sort-Object)
    bundled_doc = Join-Path $root 'bundled\doc\probe.ass'
    probe_exe = Join-Path $app 'HikariSub.exe'
    probe_doc = Join-Path $root 'probe\doc\probe.ass'
    probe = $probe
    probe_sha256 = (Get-FileHash $probe -Algorithm SHA256).Hash.ToLower()
    output = $output
    corpus = $names
}
Write-Output ('LEGACY_LAYOUT ' + ($layout | ConvertTo-Json -Compress))
