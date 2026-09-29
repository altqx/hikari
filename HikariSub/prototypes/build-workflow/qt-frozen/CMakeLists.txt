cmake_minimum_required(VERSION 3.28)
# One source tree, three stages. Provisioning stages run before project() so no
# compiler is enabled; each is its own workflow preset so the installer secret
# reaches only the install stage's process.
set(HIKARI_QT_STAGE "" CACHE STRING "acquire | install | proof")
if(HIKARI_QT_STAGE STREQUAL "acquire" OR HIKARI_QT_STAGE STREQUAL "install")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/${HIKARI_QT_STAGE}.cmake")
    project(HikariQtFrozenProvision LANGUAGES NONE)
    return()
elseif(NOT HIKARI_QT_STAGE STREQUAL "proof")
    message(FATAL_ERROR "Use a qt-frozen-linux-* workflow preset (HIKARI_QT_STAGE unset)")
endif()
project(HikariQtFrozenProof LANGUAGES CXX)
enable_testing()
add_subdirectory(proof)
