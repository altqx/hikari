---
status: accepted
---

# Use pinned vcpkg and Qt through a CMake workflow

In the live review on 2026-09-27, altqx accepted the [stateless build contract](../qt/build.md): vcpkg manifests with owned overlays/triplets, CMake-managed pinned official Qt provisioning, declared development-tool prerequisites and `cmake --workflow --preset …` to configure and build. This chooses maintained dependency recipes and explicit artifact ownership over an entirely custom CMake dependency graph or Conan tooling, while preserving no project bootstrap scripts or MSBuild path. The workflow spelling corrects the original shorthand because plain `cmake --preset` only configures. Exact locks, installer/account/license behavior, fork ABI consistency and clean Windows/Linux reconstruction remain feasibility gates; acceptance is not a build or provisioning pass.

## Follow-up decision — 2026-09-27

Altqx selected latest MSVC instead of requiring MSVC2022 and directed Linux feasibility work to GitHub Actions for now. Qualifying builds still pin their exact compiler/SDK and CI environment; this does not authorize floating dependency inputs or claim a build pass. The newly installed Qt6.11.2 MinGW SDK can support bounded native experiments, while the MSVC build still needs a compatible Qt kit and measured ABI/runtime evidence. Local Linux installation is not required for the chosen CI route.
