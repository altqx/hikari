# Thirdparty

Third-party sources the build compiles from this tree. Every other dependency
comes from vcpkg (`vcpkg.json`, `ports/`).

| Directory | Built by | Why it is vendored |
|---|---|---|
| `luabins` | `src/helpers/lua` | Three small fixes for modern LuaJIT (`luaL_reg` → `luaL_Reg`, `LUA_LIB`, `LUAI_BITSINT`); upstream is unmaintained |
| `DependencyControl` | `src/helpers/lua` | The native modules (BadMutex, PreciseTimer, DownloadManager) the automation tree loads |
| `SubInspector` | `src/helpers/lua` (Linux) | The v0.5.1 release (MIT) whose prebuilt DLL the automation tree ships for Windows; Linux builds it from these unchanged sources. Origin, commit and hashes in its [README](SubInspector/README.md) |
| `BaseClasses` | `src/backends/directshow` (Windows) | DirectShow base classes from the Windows SDK samples, locally patched |
