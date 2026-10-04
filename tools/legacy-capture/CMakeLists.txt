# Legacy characterization probes. Built against a checkout of the legacy wx
# source at the pinned baseline (LEGACY_SOURCE) and its wxWidgets build; never
# part of the Qt application build.
cmake_minimum_required(VERSION 3.21)
project(HikariLegacyCapture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(LEGACY_SOURCE "" CACHE PATH "Legacy wx source checkout at the pinned baseline")
set(WX_CONFIG "" CACHE FILEPATH "wx-config from that checkout's wxWidgets build")
if(NOT EXISTS "${LEGACY_SOURCE}/HikariSub/SubsTime.cpp" OR NOT EXISTS "${WX_CONFIG}")
    message(FATAL_ERROR "Set LEGACY_SOURCE and WX_CONFIG")
endif()
execute_process(COMMAND "${WX_CONFIG}" --cxxflags base,core OUTPUT_VARIABLE wx_cflags OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${WX_CONFIG}" --libs base,core OUTPUT_VARIABLE wx_libs OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(wx_cflags UNIX_COMMAND "${wx_cflags}")
separate_arguments(wx_libs UNIX_COMMAND "${wx_libs}")

# Timebase is wx-free; SubsTime needs wx strings only for the operators used.
add_executable(legacy_time_capture time_capture.cpp log_stub.cpp
    "${LEGACY_SOURCE}/HikariSub/Timebase.cpp"
    "${LEGACY_SOURCE}/HikariSub/SubsTime.cpp")
# Same Linux compatibility layer the legacy build uses (its root CMakeLists).
target_include_directories(legacy_time_capture PRIVATE
    "${LEGACY_SOURCE}/HikariSub" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_time_capture PRIVATE
    _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS HAVE_DLL_EXPORT
    LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_time_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_time_capture PRIVATE ${wx_libs})

# A5: the legacy Karaoke class (KaraokeSplitting.cpp, unchanged) against
# stand-ins for the GUI headers it includes. The copy sits beside nothing, so
# its quoted includes resolve to karaoke/ instead of the legacy GUI headers.
configure_file("${LEGACY_SOURCE}/HikariSub/KaraokeSplitting.cpp" "${CMAKE_CURRENT_BINARY_DIR}/karaoke/KaraokeSplitting.cpp" COPYONLY)
configure_file("${LEGACY_SOURCE}/HikariSub/KaraokeSplitting.h" "${CMAKE_CURRENT_BINARY_DIR}/karaoke/KaraokeSplitting.h" COPYONLY)
add_executable(legacy_karaoke_capture karaoke_capture.cpp "${CMAKE_CURRENT_BINARY_DIR}/karaoke/KaraokeSplitting.cpp")
target_include_directories(legacy_karaoke_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/karaoke" "${CMAKE_CURRENT_BINARY_DIR}/karaoke" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_karaoke_capture PRIVATE LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_karaoke_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_karaoke_capture PRIVATE ${wx_libs})
