# W2: the optional xy-VSFilter CSRI renderer, outside the default graph (the
# manifest feature "vsfilter", HIKARI_WITH_VSFILTER). The legacy build compiled
# the fork's MSBuild projects inside HikariSub.sln (20d647c4: HikariSub.sln:46-78,
# .github/workflows/build.yml); this recipe compiles the same Release|x64 file
# lists and settings with CMake. Inputs are pinned by archive hash:
# - the fork altqx/xy-VSFilter at c4297ed0 (Thirdparty/PATCHES.md, the
#   submodule pin at 20d647c4);
# - Thirdparty/BaseClasses from the legacy baseline 20d647c4, which the fork's
#   projects include and link (BaseClassesStatic.vcxproj), not the fork's own
#   src/filters/BaseClasses.
# Boost is vcpkg's (1.92.0, header-only flyweight/smart_ptr) where legacy
# hydrated 1.91.0 (Thirdparty/dependencies.json).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO altqx/xy-VSFilter
    REF c4297ed033ab899c86023543bd65ff08c7ba20a3
    SHA512 afd5de28ff619c640ef946f309269d25bd791c037f09f611281467fa345084c57ec73e9edcd1635a63acc0cb03b494de6e13d275643b6016bffcb89cc12b11e2
    HEAD_REF hikarisub
)
vcpkg_from_github(
    OUT_SOURCE_PATH LEGACY_PATH
    REPO altqx/hikari
    REF 20d647c4c769ab7f5d383cf3c1c33f03876a94e9
    SHA512 32f38fd74c14ec31aee1b5969aa79cab4a6cacbaa85c385c3731e5063cfdf030135141e8e4d62c4a8e0d115b98752914f2399096a1c426a73ec289e0ae7e71c2
    HEAD_REF main
)
# The VirtualDub kernels are YASM sources (src/YASM.props: yasm -X vc -f win64).
vcpkg_find_acquire_program(YASM)
vcpkg_cmake_configure(
    SOURCE_PATH "${CURRENT_PORT_DIR}"
    OPTIONS
        "-DVSFILTER_SOURCE_DIR=${SOURCE_PATH}"
        "-DBASECLASSES_SOURCE_DIR=${LEGACY_PATH}/Thirdparty/BaseClasses"
        "-DYASM_EXECUTABLE=${YASM}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME HikariXyVSFilter CONFIG_PATH share/HikariXyVSFilter)
# A plugin DLL with its own static CRT and static MFC, as legacy built it
# (UseOfMfc Static, RuntimeLibrary MultiThreaded); nothing links against it.
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled) # a plugin: no import library
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/License.txt")
