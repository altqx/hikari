---
status: accepted
---

# Use pinned vcpkg and Qt through a CMake workflow

In the live review on 2026-09-27, altqx accepted the [stateless build contract](../qt/build.md): vcpkg manifests with owned overlays/triplets, CMake-managed pinned official Qt provisioning, declared development-tool prerequisites and `cmake --workflow --preset …` to configure and build. This chooses maintained dependency recipes and explicit artifact ownership over an entirely custom CMake dependency graph or Conan tooling, while preserving no project bootstrap scripts or MSBuild path. The workflow spelling corrects the original shorthand because plain `cmake --preset` only configures. Exact locks, installer/account/license behavior, fork ABI consistency and clean Windows/Linux reconstruction remain feasibility gates; acceptance is not a build or provisioning pass.
