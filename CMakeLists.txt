cmake_minimum_required(VERSION 3.28)
# Provisioning runs before project(): it verifies the locked Qt mirror and
# installation and the pinned vcpkg checkout/tool, then sets the toolchain.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/provision/Provision.cmake")
if(HIKARI_PROVISION_ONLY OR HIKARI_QT_VERIFY_ONLY)
    project(HikariSubProvision LANGUAGES NONE)
    return()
endif()

project(HikariSub VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/HikariDependencies.cmake")
add_subdirectory(tests/dependency-load)
