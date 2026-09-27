# Windows FFMS2 source slice — observed result

**Passed for the named specimens on 2026-09-27.** The freshly built FFmpeg 7.1.2#5, zlib 1.3.2#2 and source-matched FFMS2 5.1.0 library passed the authored three-frame/private-metadata probe. An independent consumer linked only `HikariFFMS2::ffms2`, indexed the same authored input and decoded frame 0/pixel 40. This is a dependency build/link/runtime observation, not the complete build workflow or media qualification.

The host was Windows 11 Pro 10.0.26200 x64, Ryzen 5 5600 (6 cores/12 logical), 34,276,634,624 physical-memory bytes. Compiler 19.51.36260 came from MSVC directory 14.51.36231; SDK was 10.0.26100.0. The workflow used acquired, digest-verified CMake 4.4.3 and installed Ninja 1.13.2. Git registry `ee6a47da…` and tool `2026-07-27-98d7cb0…` matched the lock. The installed VS vcpkg executable was copied after official hash verification; the VS bundle and existing user binary caches stayed read-only. Binary cache reuse was disabled. See [environment](environment.json), [tool identities](acquired-tool-sha256.json), [raw recipes versus published Git blobs](recipe-identity-at-build.json) and [final observations/artifact hashes](observations.json).

## Exact successful routing

The original command `cmake --workflow --preset windows-overlay` failed under the deep workspace path. The successful run used the same preset, with **explicit supported short-directory overrides**, then separate build/test commands. It was not a bare-preset pass or a complete one-command provisioner. No user preset was generated.

From the parent `ffms2-overlay` directory, in the recorded VS x64 developer environment, the successful equivalent commands were:

```powershell
$env:HIKARI_PROTOTYPE_VCPKG_ROOT = "$PWD/_work/vcpkg"
$env:VCPKG_ROOT = $env:HIKARI_PROTOTYPE_VCPKG_ROOT
$env:VCPKG_DOWNLOADS = "$PWD/_work/downloads"
$env:X_VCPKG_REGISTRIES_CACHE = "$PWD/_work/registries"
$env:VCPKG_DEFAULT_BINARY_CACHE = "$PWD/_work/binary-cache"
$env:VCPKG_BINARY_SOURCES = 'clear'
$env:X_VCPKG_ASSET_SOURCES = 'clear'
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_MAX_CONCURRENCY = '2'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
$env:VCPKG_DEFAULT_HOST_TRIPLET = 'x64-windows'
$env:TEMP = 'C:/Work/HikariF48/temp'
$env:TMP = $env:TEMP
$cmake = "$PWD/_work/downloads/tools/cmake-4.4.3-windows/cmake-4.4.3-windows-x86_64/bin/cmake.exe"
& $cmake --preset windows-overlay -B C:/Work/HikariF48/out `
  '-DVCPKG_INSTALLED_DIR=C:/Work/HikariF48/installed' `
  '-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=C:/Work/HikariF48/b;--x-packages-root=C:/Work/HikariF48/p'
& $cmake --build C:/Work/HikariF48/out --parallel 2
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir C:/Work/HikariF48/out -C Release --output-on-failure
```

These commands require the already verified registry/tool/download preparation and an authorized unused scratch root. They are an invocation record, not a bootstrap that provisions missing prerequisites. Exact original execution scripts are retained as text: [environment](environment.ps1.txt), [successful routing](08-build-short.ps1.txt), [resource controller](Run-Bounded.ps1.txt). The controller ran those commands in an owned child process with hidden windows and stopped only that process tree if a guard fired. None fired.

The CMake consumer and port recipes remained in the prototype checkout. Dependency sources were acquired into `C:/Work/HikariF48/b`; packages, installed artifacts and build outputs used `p`, `installed` and `out` there. Downloads/registry stayed in the original `_work`. No old tree was deleted/moved, no drive was mapped and no OS long-path setting changed. The only test source copies were the separate FFMS-only consumer and deliberate corruption specimens.

The independent [consumer sources](ffms-only/CMakeLists.txt) contain no direct FFmpeg discovery/link statement. Its [exact command script](09-ffms-only.ps1.txt) used `VCPKG_MANIFEST_MODE=OFF`, the same toolchain/installed tree and `C:/Work/HikariF48/ffms-only`, then ran against the combined probe's authored Matroska file. [Full link and runtime output](ffms-only-build-and-run.txt) shows the transitive libraries supplied through the imported target.

## Failures and narrow corrections retained

- Initial vcpkg promisor fetch failed DNS; direct Git archive of the identical pinned port tree worked. The next checkout required repo-local `core.longpaths=true`. [First fetch failure](initial-promisor-fetch-failure.txt), [Git path failure](git-longpath-failure.txt).
- Actual resolution initially added avdevice/avfilter. An explicit top-level FFmpeg dependency with defaults disabled corrected that; no version changed. Both [original](resolution-original.txt) and [corrected](resolution-corrected-scratch.txt) plans are retained. Dry-run acquired tools and ran compiler detection; it was not a no-I/O operation.
- The deep-root compiler check failed with LNK1104 for `intermediate.manifest`. The authorized short-root retry passed. [Diagnostic](deep-path-compiler-failure.txt).
- Windows Git archive materialized all 20 locked text inputs as CRLF. An explicit `core.eol=lf` diagnostic archive matched every raw canonical hash. The port restores canonical LF only after the candidate matches the existing digest, then checks the written bytes again. [Archive comparison](git-archive-line-endings.json), [initial strict rejection](strict-source-hash-failure.txt). Explicit CMake `NEWLINE_STYLE LF` was necessary; the observed `file(WRITE)` emitted CRLF.
- The final real port configuration rejected a CRLF specimen with extra non-line-ending bytes, preserving the corrupt input. [Guard result](corruption-guard.json), [configure diagnostic](corruption-guard-configure.txt), [fixture procedure](10-corruption-guard.ps1.txt). The 20 lock digests and private implementation's SHA-512 were not relaxed.

## Result and limits

[CTest](private-api-and-frames.txt) passed private title/language, header/runtime version 5.1.0, three indexed frames and every pixel in request order 2/0/1; output reports FFmpeg 7.1.2. Its 0.05-second test duration is not a performance budget. [Final build log](build-final.txt) and [warnings](build-final-warnings.txt) retain the FFmpeg finder CMP0174 warning; source warnings were not silently fixed.

Across the recorded monitored phases, peak sampled scratch was 1,601,586,865 bytes, maximum whole-host received delta 797,789,562 bytes and minimum C: free 40,675,135,488 bytes. Both roots counted; all host traffic after the initial baseline was conservatively included. Samples were approximately three seconds apart, with stop margins at 1.8 GiB received/7.5 GiB scratch/26 GiB free against approved 2/8/25 GiB limits. These are sampled observations, not exact package wire bytes or calibrated performance. [Samples](budget-samples.jsonl), [retained acquisition-file hashes](acquired-files-final.json).

Still unproved here: full official Qt/CMake provisioning, replay/offline/error matrix, Qt/libass composition, actual isolated helper IPC, audio resampling, chapters/subtitle callbacks/attachments, long-GOP/VFR and broad codecs, hardware decode, clock/latency, packaging/licenses/signing/updating, and production compatibility. No binaries, downloaded dependencies, caches or authored media file are publication assets; their local identities are recorded instead. Linux has its own separately pinned run, including its direct-FFmpeg static-closure limitation.
