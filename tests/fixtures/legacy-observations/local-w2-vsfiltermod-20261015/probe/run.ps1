# W2: the legacy app's own frames with VSFilterMod drawing the fixture scripts.
# For each capture in captures.json the app starts with a fresh Config folder
# (Config.base.txt and Hotkeys.txt, nothing else: repeated runs shrink
# VIDEO_WINDOW_SIZE otherwise), the video and a copy of the fixture script
# whose "Active Line" is that capture's Comment, so the video opens at its
# time (OPEN_VIDEO_AT_ACTIVE_LINE). Then F8 ("Save frame with subtitles as
# PNG") and F7 ("Save frame as PNG") save the frame beside the video as
# <video>_<n>_<time>.png, which is moved to OUT\<fixture>-<ms>-subbed.png and
# -plain.png. A screenshot of the desktop is kept for each capture.
# env: APPDIR (the legacy package), OUT, ONLY (optional: "fixture-ms,..." to limit).
$ErrorActionPreference = 'Stop'
$app = $env:APPDIR
$exe = Join-Path $app 'HikariSub.exe'
$cfgDir = Join-Path $app 'Config'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$fixtures = Split-Path -Parent $here
$out = $env:OUT
$work = 'C:\w2cap\work'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class W2 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public static void Tap(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); keybd_event(vk, 0, 2, UIntPtr.Zero); }
}
'@
function Shot([string]$path) {
  $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
function Foreground([System.Diagnostics.Process]$p) {
  for ($i = 0; $i -lt 10; $i++) {
    $p.Refresh()
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
      [W2]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W2]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
      [void][W2]::SetForegroundWindow($p.MainWindowHandle)
      Start-Sleep -Milliseconds 500
      $fpid = 0; [void][W2]::GetWindowThreadProcessId([W2]::GetForegroundWindow(), [ref]$fpid)
      if ($fpid -eq $p.Id) { return $true }
    }
    Start-Sleep -Seconds 1
  }
  return $false
}
# Presses `vk` until a new PNG appears beside the video; its path, or $null.
function SaveWith([byte]$vk) {
  $before = @(Get-ChildItem $work -Filter 'w2-gradient_*.png' | ForEach-Object FullName)
  for ($try = 0; $try -lt 3; $try++) {
    [W2]::Tap($vk)
    for ($i = 0; $i -lt 20; $i++) {
      Start-Sleep -Milliseconds 500
      $new = @(Get-ChildItem $work -Filter 'w2-gradient_*.png' | Where-Object { $before -notcontains $_.FullName })
      if ($new.Count -gt 0) { Start-Sleep -Milliseconds 500; return $new[0] }
    }
  }
  return $null
}
New-Item -ItemType Directory -Force $out, $work | Out-Null
Get-ChildItem $work | Remove-Item -Recurse -Force
Copy-Item (Join-Path $here 'media\w2-gradient.mkv') $work
$video = Join-Path $work 'w2-gradient.mkv'
"app: $app"
"exe sha256: " + (Get-FileHash $exe).Hash
"csri: " + ((Get-ChildItem (Join-Path $app 'Csri') | ForEach-Object { "$($_.Name) $((Get-FileHash $_.FullName).Hash)" }) -join ', ')
"video sha256: " + (Get-FileHash $video).Hash
$only = if ($env:ONLY) { $env:ONLY -split ',' } else { @() }
$captures = Get-Content (Join-Path $fixtures 'captures.json') -Raw | ConvertFrom-Json
$failed = 0
foreach ($c in $captures) {
  $label = "$($c.fixture)-$($c.ms)"
  if ($only.Count -and $only -notcontains $label) { continue }
  "=== $label (Active Line $($c.activeLine))"
  if (Test-Path $cfgDir) { Remove-Item -Recurse -Force $cfgDir }
  New-Item -ItemType Directory -Force $cfgDir | Out-Null
  Copy-Item (Join-Path $here 'Config.base.txt') (Join-Path $cfgDir 'Config.txt')
  Copy-Item (Join-Path $here 'Hotkeys.txt') (Join-Path $cfgDir 'Hotkeys.txt')
  $text = [System.IO.File]::ReadAllText((Join-Path $fixtures "scripts\$($c.fixture).ass"))
  $text = $text -replace "PlayResY: 360\n", "PlayResY: 360`nActive Line: $($c.activeLine)`n"
  $subs = Join-Path $work "$label.ass"
  [System.IO.File]::WriteAllText($subs, $text, (New-Object System.Text.UTF8Encoding($true)))
  $p = Start-Process -FilePath $exe -ArgumentList "`"$subs`"", "`"$video`"" -WorkingDirectory $app -PassThru
  Start-Sleep -Seconds 12
  if (-not (Foreground $p)) { "app not in the foreground" }
  Start-Sleep -Seconds 2
  Shot (Join-Path $out "$label-screen.png")
  $subbed = SaveWith 0x77
  $plain = SaveWith 0x76
  if ($subbed) { "subbed: $($subbed.Name)"; Move-Item -Force $subbed.FullName (Join-Path $out "$label-subbed.png") } else { "subbed: none"; $failed++ }
  if ($plain) { "plain: $($plain.Name)"; Move-Item -Force $plain.FullName (Join-Path $out "$label-plain.png") } else { "plain: none"; $failed++ }
  Get-Process -Id $p.Id -ErrorAction SilentlyContinue | ForEach-Object { "running: $($_.MainWindowTitle)"; $_.Kill() }
  Start-Sleep -Seconds 2
  Copy-Item -Force (Join-Path $cfgDir 'Config.txt') (Join-Path $out "$label-Config.after.txt") -ErrorAction SilentlyContinue
}
"failed saves: $failed"
exit $failed
