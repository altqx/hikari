# Evidence harness scripts checked without a VM or a desktop.

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    # S4/S3 on Windows: drive_windows.py starts legacy in its folder and sees
    # the message boxes and dialogs it owns (a stand-in for winix ui).
    add_test(NAME legacy-capture.drive-windows
        COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/tools/legacy-capture/drive_windows_tests.py")
    set_tests_properties(legacy-capture.drive-windows PROPERTIES LABELS "tools" TIMEOUT 60)
endif()
