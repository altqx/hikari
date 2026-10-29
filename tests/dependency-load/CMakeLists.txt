add_executable(hikari_dependency_load main.cpp)
target_link_libraries(hikari_dependency_load PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Hikari::media_deps Hikari::portaudio)

add_executable(hikari_luajit_load luajit_load.cpp)
target_link_libraries(hikari_luajit_load PRIVATE Hikari::luajit)
add_test(NAME luajit-load COMMAND hikari_luajit_load)
set_tests_properties(luajit-load PROPERTIES TIMEOUT 60)
add_test(NAME dependency-load COMMAND hikari_dependency_load)
set_tests_properties(dependency-load PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
if(WIN32)
    set_property(TEST dependency-load APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${HIKARI_QT_PREFIX}/bin")
endif()

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

if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_PLUGINS)
    set(_hikari_qt_plugins "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
else()
    set(_hikari_qt_plugins "${HIKARI_QT_PREFIX}/plugins")
endif()
if(WIN32)
    set(_hikari_platform windows)
else()
    set(_hikari_platform linux)
endif()
set(_hikari_qt_manifest "${PROJECT_SOURCE_DIR}/packaging/qt-deployment.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_hikari_qt_manifest}")
add_test(NAME qt-deployment-manifest COMMAND "${CMAKE_COMMAND}"
    "-DMANIFEST=${_hikari_qt_manifest}" "-DLOCKS=${PROJECT_SOURCE_DIR}/cmake/locks"
    "-DPLUGINS=${_hikari_qt_plugins}" -DPLATFORM=${_hikari_platform}
    "-DOUT=${CMAKE_BINARY_DIR}/provision-evidence/qt-deployment-manifest.json"
    -P "${PROJECT_SOURCE_DIR}/cmake/manifest/QtDeploymentManifest.cmake")
set_tests_properties(qt-deployment-manifest PROPERTIES LABELS packaging TIMEOUT 60)
file(READ "${_hikari_qt_manifest}" _hikari_qt_manifest_text)
foreach(_control "windows;imageformats" "linux;iconengines")
    list(GET _control 0 _platform)
    list(GET _control 1 _category)
    string(JSON _without REMOVE "${_hikari_qt_manifest_text}" platforms ${_platform} ${_category})
    set(_without_file "${CMAKE_CURRENT_BINARY_DIR}/qt-deployment-without-${_platform}-${_category}.json")
    file(WRITE "${_without_file}" "${_without}")
    add_test(NAME qt-deployment-manifest-control-${_platform}-${_category} COMMAND "${CMAKE_COMMAND}"
        "-DMANIFEST=${_without_file}" "-DLOCKS=${PROJECT_SOURCE_DIR}/cmake/locks"
        "-DPLUGINS=${_hikari_qt_plugins}" -DPLATFORM=${_hikari_platform}
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/qt-deployment-control-${_platform}-${_category}.json"
        -P "${PROJECT_SOURCE_DIR}/cmake/manifest/QtDeploymentManifest.cmake")
    set_tests_properties(qt-deployment-manifest-control-${_platform}-${_category} PROPERTIES
        LABELS packaging TIMEOUT 60 PASS_REGULAR_EXPRESSION "${_platform}: the manifest does not carry ${_category}/")
endforeach()
