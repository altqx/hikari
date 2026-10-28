# Qt frozen repository: isolated official-installer run

Executes the [bounded plan](../qt-frozen-repository-plan.md) for [Prove pinned CMake workflow provisioning on Windows and Linux](https://github.com/altqx/hikari/issues/48), Linux first. Owner authorization for the pinned open-source license/obligation set is [recorded on the ticket](https://github.com/altqx/hikari/issues/48#issuecomment-5890780519); the owner-managed `QT_INSTALLER_JWT_TOKEN` repository secret exists. This is a throwaway feasibility slice, not the production build.

## What it does

| Stage | Entry | Credentials | Gate it can pass |
| --- | --- | --- | --- |
| Lock | `tools/make-lock.py` (maintainer tool, not run by the build) | none | Complete, reviewable closure of the three requested packages |
| Acquire | `cmake --workflow --preset qt-frozen-linux-acquire` | none | Every mirror object and the installer match the lock; nothing unlocked is present |
| Install | `cmake --preset qt-frozen-linux-install` | `QT_INSTALLER_JWT_TOKEN` only here | Official installer 4.11.0 installs from the local mirror; installed components are all locked |
| Proof | `cmake --workflow --preset qt-frozen-linux-proof` | none, asserted | Frozen Qt configures/builds a Quick + Multimedia + ShaderTools + Linguist consumer and passes its tests |

Provisioning runs before `project()`, so no compiler is enabled. Each stage is its own preset: a CMake workflow preset cannot change configure presets between steps, and keeping the installer in its own process is what keeps the token out of every other stage. The one-command contract therefore stays **open**, as the plan anticipated.

## Lock

The first CI run ([36575665976](https://github.com/altqx/hikari/actions/runs/36575665976)) requested the `linux_gcc_64` add-on leaves directly. They are virtual: the installer logged "Cannot install … Component is virtual", skipped both, exited 0, and the proof stage then found no Qt Multimedia. The lock now requests the add-on parents, whose leaves follow through `AutoDependOn`, and the install stage fails unless every selected package is actually installed.

[`lock/qt-linux.lock.json`](lock/qt-linux.lock.json) resolves the requested IDs through XML `Dependencies`, `AutoDependOn` and the dependencies the packages' own install scripts add at runtime. The license nodes arrive that way: `qt.tools` adds `qt.license.lgpl` and `qt.license.thirdparty`. `mocwrapper` is added by the base script only if a repository offers it; none of the frozen roots do, so it is recorded as an absent optional dependency. Ancestor nodes the controller may also select (`qt`, `qt.qt6.6112`, the add-on parents and their docs/examples) are locked as **conditional**; the run records whether each was consumed.

Qt publishes SHA-1 sidecars, not SHA-256. The lock generator streamed every object once, checked it against its publisher SHA-1 and recorded the SHA-256 it observed. That is trust on first verified acquisition over HTTPS, stated in the lock. The installer's SHA-256 matched the plan's declared value. License text digests are locked too: the install stage re-extracts them and refuses to run if they differ from the reviewed bytes.

Regenerate with `python3 tools/make-lock.py --out lock/qt-linux.lock.json --hash-dir <scratch>` (needs `7z`). A changed upstream object is a lock revision for review, never adopted by the build.

## CI run

[`qt-frozen-feasibility.yml`](../../../../.github/workflows/qt-frozen-feasibility.yml) runs on pushes to this branch touching this directory, or manually. It has `contents: read`, SHA-pinned actions, no persisted checkout credentials, no PR trigger, no caches and a 60-minute limit. In order:

1. Declared host packages (Mesa, xcb, Xvfb, PulseAudio/VA client libraries) are installed and their versions recorded.
2. Cold acquisition.
3. One mirror object is corrupted: verification must reject it, then a warm re-acquisition must refetch only that object.
4. The mirror is served on loopback with an access log.
5. The installer step, the only one with the secret, runs with a private `HOME` that is deleted afterwards. Only a sanitized excerpt of its log is kept: progress and error lines, with e-mail addresses and token-like strings redacted.
6. The mirror access log is reduced to which locked objects were actually served.
7. The proof stage first asserts that the token and the account file are absent.

The uploaded artifact contains `_evidence/` only: no SDK, mirror, raw installer log or account data.

## Local smoke check

The proof sources were compiled against Arch's Qt 6.11.2 packages in a scratch copy with the frozen-origin check removed. The offscreen selftest passed (runtime 6.11.2, 13 decode formats, QML module loaded). The render test cannot pass offscreen: Qt falls back to the software scene graph, which cannot run `ShaderEffect`, and the test says so explicitly. It runs under Xvfb in CI. The distro Qt is not evidence for this task, and the proof stage refuses any Qt outside `_work/qt`.

## Not established here

Windows, Fedora, native desktop sessions, deployment, cold/warm reconstruction of the full dependency graph (Qt with FFMS2/libass), offline prefetch and release signing stay separate gates. Loopback serving shows which mirror objects the installer read, not all of its outbound traffic: it still contacts Qt's account service to authenticate. Whether the run passes is decided by the CI evidence, not by this README.
