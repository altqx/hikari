# V3 (#182) on Windows (winix task legacy-win-next-file): legacy
# VideoBox::NextFile's folder walk on NTFS, through the same probe as Linux
# (tools/legacy-capture/next_file). The legacy baseline is fetched at its
# pinned commit with only its wxWidgets submodule (v3.3.3) into
# out/legacy-win/next-file (nothing is installed), the probe is built with
# MSVC against that wx's static wxbase, and it runs on a fresh folder under
# out/legacy-win/next-file/run. The observation is printed and kept as
# out/legacy-win/next-file/windows.json (the task's artifact); reviewed, it
# goes to tests/fixtures/legacy-observations/local-v3-next-file-20261013/.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$commit = '20d647c4c769ab7f5d383cf3c1c33f03876a94e9'
$repo = 'https://github.com/altqx/hikari.git'
$root = Join-Path $PWD 'out\legacy-win\next-file'
$legacy = Join-Path $root 'legacy'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $root | Out-Null

function Invoke-Checked {
    $command = $args[0]
    $rest = @($args | Select-Object -Skip 1)
    & $command @rest
    if ($LASTEXITCODE) { throw "$($args -join ' ') failed ($LASTEXITCODE)" }
}

if (-not (Test-Path (Join-Path $legacy 'HikariSub\VideoBox.cpp'))) {
    if (Test-Path $legacy) { Remove-Item -Recurse -Force $legacy }
    Invoke-Checked git init -q $legacy
    Invoke-Checked git -C $legacy remote add origin $repo
    Invoke-Checked git -C $legacy fetch -q --depth 1 origin $commit
    Invoke-Checked git -C $legacy -c advice.detachedHead=false checkout -q FETCH_HEAD
}
$head = (git -C $legacy rev-parse HEAD).Trim()
if ($head -ne $commit) { throw "legacy checkout at $head, not $commit" }
if (-not (Test-Path (Join-Path $legacy 'Thirdparty\wxWidgets\CMakeLists.txt'))) {
    # The wx pin only; its own submodules are not needed (no regex, zlib or expat).
    Invoke-Checked git -C $legacy submodule update -q --init --depth 1 Thirdparty/wxWidgets
}
Write-Output "legacy $head, wxWidgets $((git -C $legacy rev-parse HEAD:Thirdparty/wxWidgets).Trim())"

# extract_functions.py needs a Python 3: the PATH's, else vcpkg's own.
$python = @(
    (Get-Command python -ErrorAction SilentlyContinue | Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1 -ExpandProperty Source),
    (Get-ChildItem -Recurse -Filter python.exe 'out\sdk\vcpkg\downloads\tools' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName)
) | Where-Object { $_ } | Select-Object -First 1
if (-not $python) { throw 'no Python 3 for extract_functions.py (none on PATH, none in out\sdk\vcpkg\downloads\tools)' }

Invoke-Checked ./tools/winix/msvc.ps1 cmake -S tools/legacy-capture/next_file -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DLEGACY_SOURCE=$legacy" "-DPython3_EXECUTABLE=$python"
Invoke-Checked ./tools/winix/msvc.ps1 cmake --build $build --target legacy_next_file_capture

$run = Join-Path $root 'run'
if (Test-Path $run) {
    Get-ChildItem -Recurse -Force $run | ForEach-Object { $_.Attributes = 'Normal' }
    Remove-Item -Recurse -Force $run
}
$out = Join-Path $root 'windows.json'
$exe = Join-Path $build 'legacy_next_file_capture.exe'
$names = Join-Path $PWD 'tools\legacy-capture\inputs\next-file-names.txt'
$p = Start-Process -FilePath $exe -ArgumentList "`"$names`" `"$run`"" -NoNewWindow -PassThru -Wait `
    -RedirectStandardOutput $out -RedirectStandardError (Join-Path $root 'stderr.txt')
Get-Content (Join-Path $root 'stderr.txt') -ErrorAction SilentlyContinue
if ($p.ExitCode) { throw "the probe failed ($($p.ExitCode))" }
Get-Content -Encoding UTF8 $out
Write-Output "sha256 $((Get-FileHash $out -Algorithm SHA256).Hash.ToLower())"
