# V3 (#182): legacy VideoBox::NextFile's folder walk (next_file_capture.cpp).
# A project of its own so that it builds the same way on Linux and on Windows
# (MSVC): the pinned wxWidgets source (the legacy checkout's
# Thirdparty/wxWidgets submodule, v3.3.3) is built here as a static wxbase
# without the GUI, and VideoBox::NextFile is copied out of the legacy
# VideoBox.cpp unchanged.
#
#   cmake -S tools/legacy-capture/next_file -B <build> -DLEGACY_SOURCE=<legacy checkout>
#   cmake --build <build> --target legacy_next_file_capture
cmake_minimum_required(VERSION 3.21)
project(HikariLegacyNextFile LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(LEGACY_SOURCE "" CACHE PATH "Legacy source checkout at the pinned baseline")
set(WX_SOURCE "${LEGACY_SOURCE}/Thirdparty/wxWidgets" CACHE PATH "wxWidgets source (the legacy pin, v3.3.3)")
if(NOT EXISTS "${LEGACY_SOURCE}/HikariSub/VideoBox.cpp" OR NOT EXISTS "${WX_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Set LEGACY_SOURCE (and WX_SOURCE when the submodule is elsewhere)")
endif()

# wxbase alone, static; nothing it does not need for wxDir and wxString.
set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxUSE_GUI OFF CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES OFF CACHE STRING "" FORCE)
set(wxBUILD_TESTS OFF CACHE STRING "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(wxBUILD_PRECOMP OFF CACHE STRING "" FORCE)
set(wxUSE_REGEX OFF CACHE STRING "" FORCE)
set(wxUSE_ZLIB OFF CACHE STRING "" FORCE)
set(wxUSE_EXPAT OFF CACHE STRING "" FORCE)
set(wxUSE_LIBLZMA OFF CACHE BOOL "" FORCE)
set(wxUSE_SECRETSTORE OFF CACHE STRING "" FORCE)
set(wxUSE_WEBREQUEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${WX_SOURCE}" wx EXCLUDE_FROM_ALL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${gen}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../extract_functions.py"
    "${LEGACY_SOURCE}/HikariSub/VideoBox.cpp" "${gen}/next_file.inc"
    "void VideoBox::NextFile(bool next)" COMMAND_ERROR_IS_FATAL ANY)

add_executable(legacy_next_file_capture next_file_capture.cpp)
target_include_directories(legacy_next_file_capture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${gen}")
target_link_libraries(legacy_next_file_capture PRIVATE wx::base)
if(MSVC)
    target_compile_options(legacy_next_file_capture PRIVATE /utf-8)
endif()
