if(UNIX)
    add_test(NAME tools.winix-hotplug-script
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/hotplug_script_test.sh"
            "${HikariSub_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/winix-hotplug")
    set_tests_properties(tools.winix-hotplug-script PROPERTIES LABELS "tools" TIMEOUT 60)
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME tools.desktop-dependencies
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/desktop_dependencies_tests.py")
    set_tests_properties(tools.desktop-dependencies PROPERTIES LABELS "tools;packaging" TIMEOUT 60)
    add_test(NAME tools.make-update-manifest
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/make_update_manifest_tests.py")
    set_tests_properties(tools.make-update-manifest PROPERTIES LABELS "tools" TIMEOUT 60)
endif()
