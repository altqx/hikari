# Release-only static native libraries with the dynamic MSVC CRT.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
# The provisioned Qt prefix reaches ports that build against Qt (spix); tracked,
# so a different Qt changes the package ABI hash.
set(VCPKG_ENV_PASSTHROUGH HIKARI_QT_PREFIX)
# Test frameworks are compiled with the project's language standard, so their
# library and the tests agree on char8_t and other C++20+ features regardless
# of each compiler's default standard.
if(PORT STREQUAL "gtest")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_CXX_STANDARD=23 -DCMAKE_CXX_STANDARD_REQUIRED=ON)
endif()
