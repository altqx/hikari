# Y8W: the Windows font fixture (tests/support/fonts/windows_user_fonts.h)
# leaves nothing installed. What a run that never reached its TearDown leaves
# (base.ttf and legacy.ttf in the user's font folder, base.ttf's HKCU Fonts
# value) is planted first; the fonts tests then run, and afterwards the
# folder must hold none of the fixtures and HKCU no value of the tests'.
$ErrorActionPreference = 'Stop'
$folder = Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'
$key = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Fonts'
$names = 'base.ttf', 'base-bold.ttf', 'weighted.ttf', 'legacy.ttf', 'collection.ttc'

function Get-Leftovers {
    $files = @($names | Where-Object { Test-Path -LiteralPath (Join-Path $folder $_) })
    $values = @((Get-Item -LiteralPath $key -ErrorAction SilentlyContinue).Property |
        Where-Object { $_ -like 'Hikari test font *' })
    [pscustomobject]@{ Files = $files; Values = $values }
}

function Show-Leftovers($when, $state) {
    Write-Output "== ${when}: $($state.Files.Count) fixture files in $folder [$($state.Files -join ', ')]; $($state.Values.Count) HKCU Fonts values [$($state.Values -join ', ')]"
}

$fixtures = Get-ChildItem -Recurse -Directory -Filter font-fixtures out/build/windows-x64-release |
    Where-Object { Test-Path (Join-Path $_.FullName 'base.ttf') } | Select-Object -First 1
if (-not $fixtures) { Write-Output 'no generated font fixtures; build first'; exit 2 }

Show-Leftovers 'before' (Get-Leftovers)
New-Item -ItemType Directory -Force -Path $folder | Out-Null
foreach ($name in 'base.ttf', 'legacy.ttf') {
    Copy-Item -LiteralPath (Join-Path $fixtures.FullName $name) -Destination (Join-Path $folder $name) -Force
}
# Never New-Item -Force on the key: that recreates it without its values.
if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key | Out-Null }
Set-ItemProperty -LiteralPath $key -Name 'Hikari test font base.ttf (TrueType)' -Value (Join-Path $folder 'base.ttf')
Show-Leftovers 'planted' (Get-Leftovers)

$ErrorActionPreference = 'Continue'
./tools/winix/msvc.ps1 ctest --preset windows-x64-release -R "^(FontCollectorRenderer|FontFixtureFiles)[.]" -V
$code = $LASTEXITCODE
Write-Output "== ctest exit $code"

$after = Get-Leftovers
Show-Leftovers 'after' $after
if ($after.Files.Count -or $after.Values.Count) { exit 1 }
exit $code
