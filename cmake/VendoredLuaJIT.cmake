# Builds the vendored LuaJIT (Thirdparty/luajit + Thirdparty/LuaJIT) the way the
# Windows build does in Thirdparty/Build/LuaJit/LuaJit.vcxproj: a static library
# with the Lua 5.2 extensions (LUAJIT_ENABLE_LUA52COMPAT: table.pack/unpack,
# __pairs/__ipairs/__len, break anywhere in a block, ...) and without
# string.buffer (LUAJIT_DISABLE_BUFFER).  Automation scripts written against
# the Windows build (DependencyControl's Functional, ASSFoundation, LibLyger,
# MoonScript's `continue`) rely on those extensions, and distribution LuaJIT
# packages are built without them.
#
# The two Thirdparty directories differ only in case and hold disjoint halves of
# one LuaJIT tree (see Thirdparty/README.md); on Windows they merge on disk.  Here
# both are copied into one tree in the build directory and LuaJIT's own Makefile
# builds it.  XCFLAGS reaches buildvm as well as the library.
#
# Defines the INTERFACE target hikarisub_luajit (headers + libluajit.a).
include_guard(GLOBAL)

find_program(HIKARISUB_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(_hikarisub_luajit_thirdparty "${CMAKE_CURRENT_LIST_DIR}/../Thirdparty")
get_filename_component(_hikarisub_luajit_thirdparty "${_hikarisub_luajit_thirdparty}" ABSOLUTE)
set(HIKARISUB_LUAJIT_XCFLAGS "-DLUAJIT_ENABLE_LUA52COMPAT -DLUAJIT_DISABLE_BUFFER")
set(_hikarisub_luajit_tree "${CMAKE_BINARY_DIR}/luajit")
set(_hikarisub_luajit_lib "${_hikarisub_luajit_tree}/src/libluajit.a")

file(GLOB_RECURSE _hikarisub_luajit_inputs CONFIGURE_DEPENDS
    "${_hikarisub_luajit_thirdparty}/luajit/src/*"
    "${_hikarisub_luajit_thirdparty}/luajit/dynasm/*"
    "${_hikarisub_luajit_thirdparty}/LuaJIT/src/*"
    "${_hikarisub_luajit_thirdparty}/LuaJIT/dynasm/*")
list(FILTER _hikarisub_luajit_inputs EXCLUDE REGEX "/\\.gitignore$")
list(APPEND _hikarisub_luajit_inputs "${_hikarisub_luajit_thirdparty}/LuaJIT/.relver")

cmake_host_system_information(RESULT _hikarisub_luajit_jobs QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT _hikarisub_luajit_jobs OR _hikarisub_luajit_jobs LESS 1)
    set(_hikarisub_luajit_jobs 1)
endif()

# The headers are read from the merged tree, where the build also generates
# luajit.h; create the directory now so the include path exists at configure.
file(MAKE_DIRECTORY "${_hikarisub_luajit_tree}/src")

add_custom_command(
    OUTPUT "${_hikarisub_luajit_lib}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_hikarisub_luajit_thirdparty}/luajit" "${_hikarisub_luajit_tree}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_hikarisub_luajit_thirdparty}/LuaJIT" "${_hikarisub_luajit_tree}"
    COMMAND "${HIKARISUB_MAKE_EXECUTABLE}" -C "${_hikarisub_luajit_tree}/src"
            -j${_hikarisub_luajit_jobs}
            BUILDMODE=static
            "CC=${CMAKE_C_COMPILER}"
            "XCFLAGS=${HIKARISUB_LUAJIT_XCFLAGS}"
            libluajit.a
    DEPENDS ${_hikarisub_luajit_inputs}
    COMMENT "Building the vendored LuaJIT (Lua 5.2 extensions, no string.buffer)"
    VERBATIM)
add_custom_target(hikarisub_luajit_build DEPENDS "${_hikarisub_luajit_lib}")

add_library(hikarisub_luajit INTERFACE)
target_include_directories(hikarisub_luajit INTERFACE "${_hikarisub_luajit_tree}/src")
target_link_libraries(hikarisub_luajit INTERFACE "${_hikarisub_luajit_lib}" ${CMAKE_DL_LIBS} m)
add_dependencies(hikarisub_luajit hikarisub_luajit_build)
