# Run 36577045109: frozen official Qt on Ubuntu 26.04, passed

[GitHub Actions run](https://github.com/altqx/hikari/actions/runs/36577045109), source commit `2e568a22`, 2026-09-29. Sanitized evidence exactly as uploaded; no SDK, mirror, raw installer log or account data.

| Gate | Result |
| --- | --- |
| Lock and acquisition | 125 locked objects, 9 repository indexes and the installer acquired and verified (declared 487,623,749 bytes). |
| Corrupt-input rejection | Appending one byte to the locked `Licenses.7z` failed verification (`verify-rejections.tsv`). Warm re-acquisition refetched only that object: 134 cached, 1 refetched. |
| Official installer | 4.11.0 against the loopback mirror, exit 0. Installed 18 components, every one locked; the full selected closure is present. 2,242,581,005 bytes on disk. |
| Consumption | All 125 locked objects and all 9 indexes were requested, all HTTP 200; no request outside the lock. The installer adds `//` and `?n` cache busters, so this was recomputed from `mirror-requests.tsv` with normalized paths. The workflow's `mirror-consumption.tsv` in this run used exact matching and wrongly shows nothing served; the parser is fixed for later runs. |
| Credential isolation | The proof step asserted that the token variable, the account file and the private installer HOME were all absent. |
| Proof | Qt6_DIR resolved inside the frozen installation, GCC 15.2.0. Offscreen selftest: runtime 6.11.2, compiled translation, qsb resource, 11 media decode formats, QML module. Xvfb/xcb render: OpenGL scene graph ran the compiled shader, centre pixel `ffff00ff` (magenta). |

The first run, [36575665976](https://github.com/altqx/hikari/actions/runs/36575665976), requested the virtual `linux_gcc_64` add-on leaves. The installer refused them ("Component is virtual"), still exited 0, and Multimedia was missing at proof time. That is why the lock now requests the parents and the install stage requires the whole closure.

## Not established

This is one headless Ubuntu runner, with host packages from its archive. Windows, Fedora, native desktops, deployment, offline prefetch and cold reconstruction on a clean non-CI machine remain separate gates. The runner image is observed, not pinned. Authentication still reaches Qt's account service; only the payload path is closed.
