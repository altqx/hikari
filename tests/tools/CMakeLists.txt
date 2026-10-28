# Evidence harness scripts (tools/) checked without a VM or a desktop, run
# against stand-ins for the tools they drive.

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    # S4/S3 on Windows: drive_windows.py starts legacy in its folder and sees
    # the message boxes and dialogs it owns (a stand-in for winix ui).
    add_test(NAME legacy-capture.drive-windows
        COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/tools/legacy-capture/drive_windows_tests.py")
    set_tests_properties(legacy-capture.drive-windows PROPERTIES LABELS "tools" TIMEOUT 60)
endif()

# N6: tools/winix/hotplug.sh plugs the USB audio device in before the task,
# also when Winix still holds a record of a device a VM restart dropped.
if(UNIX)
    add_test(NAME tools.winix-hotplug-script
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/hotplug_script_test.sh"
            "${HikariSub_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/winix-hotplug")
    set_tests_properties(tools.winix-hotplug-script PROPERTIES LABELS "tools" TIMEOUT 60)
endif()

# RL1 (#460): the release's update manifest, its offer and the offer_to guard.
if(Python3_Interpreter_FOUND)
    add_test(NAME tools.make-update-manifest
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/make_update_manifest_tests.py")
    set_tests_properties(tools.make-update-manifest PROPERTIES LABELS "tools" TIMEOUT 60)
endif()
