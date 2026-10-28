# Qt baseline and Windows 10 support proposal

**Status: reviewed alternatives for [Set the Qt baseline and Windows 10 maintenance policy](https://github.com/altqx/hikari/issues/37). The user accepted the baseline and chose C on 2026-09-27: advance Qt and explicitly retire Windows 10 later.** The authoritative [platform policy](../platform-policy.md) records this choice; the recommendation of A below was not selected. Checked 2026-09-27 against official Qt sources and the [completed survey](https://github.com/altqx/hikari/blob/3c1e557af5deb52a4fcfdc3f169b9da7afa67776/docs/research/qt6.md). No upgrade or platform qualification has been performed here.

## Immediate baseline

Recommend pinning **Qt 6.11.2 now** for prototypes and the initial reproducible build, then adopting **6.12 stable for the first LTS-based release only after publication and the gates below**. Qt's [6.11 schedule](https://wiki.qt.io/Qt_6.11_Release) records .2 released August 18; .3 is merely planned for September 28. The [6.12 schedule](https://wiki.qt.io/Qt_6.12_Release) records its RC on September 18 and final planned for September 30 with no realized final date. A calendar target is not a released artifact.

Qt's current matrix lists Windows 10 1809+ and Windows 11 x64 with MSVC 2022 or MinGW-w64 13.1, and says **6.12 is the last Windows 10 version**. These are Qt qualifications, not proof that HikariSub's complete dependency stack works. [Official platform matrix](https://doc.qt.io/qt-6/supported-platforms.html).

Keep Windows 10, Windows 11 and Linux first-class release targets. Recommend MSVC 2022 as the initial Windows kit; pin its full compiler/SDK revision and named Linux distro/compiler/glibc jobs in the build decision. Additional compilers, architectures and distro combinations need explicit qualification rather than an open-ended support promise. Preserve portable interfaces and a later macOS path; macOS delivery is not a prerequisite for the initial Windows/Linux release.

## Policy choice after the last compatible Qt line

| Choice | What it means | Tradeoff |
| --- | --- | --- |
| **A. Shared Qt ceiling — recommended** | Keep the shared application on the last validated Windows-10-compatible Qt series, expected to be 6.12. Newer Qt features do not enter mainline without reopening this policy. | Preserves one feature line and first-class Windows 10; HikariSub must maintain its Qt patch set as public upstream maintenance narrows. |
| **B. Separate maintenance line** | Keep a Windows 10 branch on that Qt series while the primary feature line advances. | Two build/release/test matrices and backports; Windows 10 eventually receives maintenance rather than all new features. Requires accepting that reduced parity. |
| **C. Follow newer Qt and retire Windows 10** | Advance past the last compatible series after an explicit support-change decision and migration notice. | Lowest long-term old-platform burden, but changes the stated first-class Windows 10 requirement. No retirement date is proposed. |

**Decision requested:** accept the immediate baseline/upgrade gates and choose A, B or C. Recommendation A favours the current Windows 10 commitment over automatic newest-Qt adoption; it does not promise indefinite maintenance without an owner.

## Upgrade and maintenance gates

Before changing the pinned version, verify public source/artifact availability and the actual supported-platform matrix; rebuild FFMS2, libass and selected adapters; run clean-machine packaging plus QML/input/IME/accessibility, media-clock, custom-rendering and performance regressions. Inspect private/QRhi API changes explicitly. Record failures and rollback to the previous reproducible kit. Apply the same relevant checks to patch updates; publication alone does not approve adoption.

Qt states that early LTS patches are public but immediate access to extended LTS updates is commercial. Its five-year LTS support term is **not five years of guaranteed free patches**. This proposal assumes no purchase or commercial entitlement. [Qt release and support policy](https://doc.qt.io/qt-6/qt-releases.html).

For a retained line, name a HikariSub maintainer responsible for public/community patch provenance, backport review, dependency advisories, reproducible builds and regression testing. Review feasibility at each release and when a critical dependency loses support; do not assume every upstream fix can be backported. If necessary fixes or qualified tools become unavailable, stop claiming the affected release passes support gates and reopen the policy with the user. Neither silent abandonment nor an invented hard EOL date is authorized.
