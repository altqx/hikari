find_package(Python3 3.9 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME crash_tools
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py")
    set_tests_properties(crash_tools PROPERTIES LABELS "tools" TIMEOUT 60)
endif()
