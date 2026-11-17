find_package(Python3 3.9 COMPONENTS Interpreter)
find_program(HIKARI_CARGO cargo)
set(dwarf_reduce_target "${CMAKE_CURRENT_BINARY_DIR}/dwarf_reduce")
if(HIKARI_CARGO)
    add_test(NAME tools.dwarf-reduce
        COMMAND "${HIKARI_CARGO}" test --release --locked
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/dwarf_reduce/Cargo.toml" --target-dir "${dwarf_reduce_target}")
    set_tests_properties(tools.dwarf-reduce PROPERTIES LABELS "tools" TIMEOUT 600 FIXTURES_SETUP dwarf_reduce)
endif()
if(Python3_Interpreter_FOUND)
    add_test(NAME crash_tools
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py")
    set_tests_properties(crash_tools PROPERTIES LABELS "tools" TIMEOUT 60 FIXTURES_REQUIRED dwarf_reduce
        ENVIRONMENT "HIKARI_DWARF_REDUCE=${dwarf_reduce_target}/release/dwarf-reduce")
endif()
if(UNIX)
    add_test(NAME tools.winix-hotplug-script
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/hotplug_script_test.sh"
            "${HikariSub_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/winix-hotplug")
    set_tests_properties(tools.winix-hotplug-script PROPERTIES LABELS "tools" TIMEOUT 60)
endif()
