# First Linux preflight observation

**Preflight capture succeeded; dependency build remains blocked and unattempted.** [Actions run 36299849291](https://github.com/altqx/hikari/actions/runs/36299849291), attempt 1, executed [source 4959243f](https://github.com/altqx/hikari/tree/4959243fefdf0737e655985e300ff9f20e12d9ac/HikariSub/prototypes/build-workflow/linux-ci) on 2026-09-27. The job ran 06:21:27–06:21:39 UTC. This duration is not a build benchmark.

The sanitized artifact is retained in [evidence/run-36299849291](evidence/run-36299849291/report.md); [report.json](evidence/run-36299849291/report.json) contains the observations and [provenance.json](evidence/provenance.json) identifies the run, GitHub-declared artifact digest and locally hashed extracted files. No installer/SDK binaries, account files, secret values or full environment dumps are retained. Public metadata is preserved exactly, not silently refreshed.

| Host/tool | Observed value |
| --- | --- |
| Distribution / runner image | Ubuntu 26.04.1 LTS x64; `ubuntu26`, `20260920.143.1` |
| Kernel / glibc | Linux 7.0.0-1012-azure; glibc 2.43 |
| GCC / G++ | 15.2.0, executable banner `Ubuntu 15.2.0-16ubuntu1` |
| CMake / Ninja / Git | 4.4.3 / 1.13.2 / 2.55.0 |
| pkg-config / Python | 2.5.1 / 3.14.4 |
| Make / Autoconf / Automake | 4.4.1 / 2.72 / 1.18.1 |
| Resources | 4 logical processors, 15,983 MiB physical RAM; `df` reported 95,817,172 KiB available (about 91.4 GiB) |

These describe one mutable runner image. They are neither locked compiler inputs nor the accepted physical performance reference machines. Do not infer future disk capacity from this snapshot.

NASM and Meson were absent from PATH. The `libtool` executable was also absent, although `dpkg-query` reported package `libtool 2.5.4-9`; this does not establish whether the recipe's actual required `libtoolize`/generated-script path is available. `unzip --version` returned 10, but package `unzip 6.0-29ubuntu1` was present: that version-query spelling is unsuitable, not evidence that unzip is missing. The aggregate package query returned 1 and partial output. CMake/Ninja were available despite no corresponding package rows; omitted package rows must not be generalized into absent executables or missing shared libraries. Exact Qt runtime/platform-plugin dependency closure remains untested. No host packages were installed.

The exact Qt repository metadata snapshot and all nine publisher checksum declarations matched the committed candidate. Downloaded `Updates.xml` was independently rehashed after artifact retrieval; the artifact's candidate lock and checksum declarations also match the source lock. Actual installer/archive binaries were neither downloaded nor verified. The package remains `qt.qt6.6112.linux_gcc_64`, revision `6.11.2-0-202608131018`, with Creator/docs/examples dependencies outside the partial lock.

The report records no CI Qt authentication declaration, no terms-acceptance scope, incomplete artifact closure, no provisioned vcpkg graph and no built FFMS2 private ABI. The CMake workflow completed its configure and report-target steps with languages `NONE`. It did not run an installer or compiler build, accept terms, exercise Quick/Linguist/libass/media, or prove cold/warm reconstruction, deployment, Fedora, Wayland, accessibility or performance.

Authoring code stays unchanged after this run. Follow-up should correct the unzip version query and characterize the actual recipe host-tool requirements before treating the host inventory as a prerequisite verdict. Provisioning still needs an authorized CI account/terms path, full verified artifact closure and the owned compatible FFMS2/FFmpeg/libass graph. A successful host/metadata preflight does not resolve those gates.
