# WS2 (#216): the fixtures with and without [Hikari] through each CSRI
# renderer at hand, read from the file as a player reads it (csri-probe.cpp).
# Renderers: the legacy release's Csri folder (xy-VSFilter_hikarisub.dll and
# VSFiltermod.dll), when extracted into out\legacy-win\bundled, and the pinned
# VSFilterMod a windows-x64-vsfiltermod build fetched, when there is one.
# Each pair's frames must hash the same. Run inside the MSVC environment
# (tools/winix/msvc.ps1); OUT (default out\ws2-csri) takes the build and log.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$fixtures = Split-Path -Parent $here
$out = if ($env:OUT) { $env:OUT } else { Join-Path $PWD 'out\ws2-csri' }
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
cl /nologo /EHa /O2 /W3 (Join-Path $here 'csri-probe.cpp') /Fe:csri-probe.exe | Out-Null
Pop-Location
if ($LASTEXITCODE -ne 0) { throw 'csri-probe did not build' }
$probe = Join-Path $out 'csri-probe.exe'

$renderers = @()
$legacy = Join-Path $PWD 'out\legacy-win\bundled\HikariSub_x64\Csri'
if (Test-Path $legacy) { $renderers += Get-ChildItem $legacy -Filter '*.dll' | ForEach-Object FullName }
$pinned = Join-Path $PWD 'out\build\windows-x64-vsfiltermod\tests\backends\vsfiltermod-csri\VSFiltermod.dll'
if (Test-Path $pinned) { $renderers += $pinned }
if (-not $renderers) { throw 'no CSRI renderer: build windows-x64-vsfiltermod first' }

$pairs = @(@('plain.ass', 'plain-hikari.ass'), @('fonts.ass', 'fonts-hikari.ass'))
$failed = 0
foreach ($dll in $renderers) {
    $hash = (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower()
    Write-Output "== $dll sha256 $hash"
    foreach ($pair in $pairs) {
        $a = Join-Path $fixtures $pair[0]
        $b = Join-Path $fixtures $pair[1]
        $lines = & $probe $dll $a $b
        $lines | ForEach-Object { Write-Output "  $_" }
        $left = @($lines | Where-Object { $_.StartsWith($a + ' ') } | ForEach-Object { $_.Substring($a.Length) })
        $right = @($lines | Where-Object { $_.StartsWith($b + ' ') } | ForEach-Object { $_.Substring($b.Length) })
        $drawn = @($left | Where-Object { $_ -notmatch 'changed=0$' }).Count
        if ($left.Count -eq 6 -and $drawn -gt 0 -and (Compare-Object $left $right -SyncWindow 0) -eq $null) {
            Write-Output "  SAME $($pair[0]) $($pair[1]) ($drawn of 6 frames drawn)"
        } else {
            Write-Output "  DIFFERENT $($pair[0]) $($pair[1])"
            $failed++
        }
    }
}
Copy-Item $fixtures\*.ass $out
exit $failed
