# The Windows unit tests (tests/CMakeLists.txt, hikarisub_windows_tests) on
# the Winix VM: configure, build and run them. HIKARISUB_TEST_NETWORK=1 adds
# the real fetch of a release's SHA256SUMS from GitHub.

$ErrorActionPreference = 'Stop'
$build = Join-Path $env:TEMP 'hs-windows-unit-tests'
cmake -S tests -B $build
if ($LASTEXITCODE -ne 0) { exit 1 }
cmake --build $build --config Release --target hikarisub_windows_tests
if ($LASTEXITCODE -ne 0) { exit 1 }
$exe = Get-ChildItem -Recurse -Path $build -Filter hikarisub_windows_tests.exe | Select-Object -First 1
& $exe.FullName
exit $LASTEXITCODE
