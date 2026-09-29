# Combined load of the provisioned Qt, FFMS2 and libass (B48-combined).
add_executable(hikari_dependency_load main.cpp)
target_link_libraries(hikari_dependency_load PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Hikari::media_deps)
add_test(NAME dependency-load COMMAND hikari_dependency_load)
set_tests_properties(dependency-load PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
if(WIN32)
    # Put the provisioned Qt runtime on PATH for the test only.
    set_property(TEST dependency-load APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${HIKARI_QT_PREFIX}/bin")
endif()
