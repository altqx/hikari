# Release-only static native libraries with the dynamic MSVC CRT.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
# The provisioned Qt prefix reaches ports that build against Qt (spix); tracked,
# so a different Qt changes the package ABI hash.
set(VCPKG_ENV_PASSTHROUGH HIKARI_QT_PREFIX)
