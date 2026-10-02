# Combined load of the provisioned Qt, FFMS2 and libass (B48-combined), and
# PortAudio (B4).
add_executable(hikari_dependency_load main.cpp)
target_link_libraries(hikari_dependency_load PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Hikari::media_deps Hikari::portaudio)

# LuaJIT loads in its own process, as in the Lua helper (B4).
add_executable(hikari_luajit_load luajit_load.cpp)
target_link_libraries(hikari_luajit_load PRIVATE Hikari::luajit)
add_test(NAME luajit-load COMMAND hikari_luajit_load)
set_tests_properties(luajit-load PROPERTIES TIMEOUT 60)
add_test(NAME dependency-load COMMAND hikari_dependency_load)
set_tests_properties(dependency-load PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
if(WIN32)
    # Put the provisioned Qt runtime on PATH for the test only.
    set_property(TEST dependency-load APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${HIKARI_QT_PREFIX}/bin")
endif()

# B4: each pinned Phase 3 input refuses a changed archive (offline, private
# work directory). PortAudio comes from the owned overlay, LuaJIT from the
# baseline port tree.
cmake_path(GET CMAKE_TOOLCHAIN_FILE PARENT_PATH _hikari_vcpkg_buildsystems)
cmake_path(GET _hikari_vcpkg_buildsystems PARENT_PATH _hikari_vcpkg_scripts)
cmake_path(GET _hikari_vcpkg_scripts PARENT_PATH _hikari_vcpkg_root)
foreach(_port_dir "${PROJECT_SOURCE_DIR}/ports/portaudio" "${_hikari_vcpkg_root}/ports/luajit")
    cmake_path(GET _port_dir FILENAME _port)
    add_test(NAME changed-input-${_port} COMMAND "${CMAKE_COMMAND}"
        "-DVCPKG=${_hikari_vcpkg_root}/vcpkg${CMAKE_EXECUTABLE_SUFFIX}"
        "-DPORT_DIR=${_port_dir}" "-DDOWNLOADS=${_hikari_vcpkg_root}/downloads"
        "-DTRIPLETS=${PROJECT_SOURCE_DIR}/triplets" "-DTRIPLET=${VCPKG_TARGET_TRIPLET}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/changed-input-${_port}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/changed_input.cmake")
    set_tests_properties(changed-input-${_port} PROPERTIES TIMEOUT 300 LABELS provisioning RUN_SERIAL TRUE)
endforeach()
