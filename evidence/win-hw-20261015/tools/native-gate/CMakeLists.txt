# The native gate runs outside the build (run.sh). Its session-free parts,
# gatelib.py, have tests that need only Python.
find_package(Python3 3.9 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME native_gate_lib
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py")
    set_tests_properties(native_gate_lib PROPERTIES LABELS "tools" TIMEOUT 60)
endif()
