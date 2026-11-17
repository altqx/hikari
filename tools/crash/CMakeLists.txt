find_package(Python3 3.9 COMPONENTS Interpreter)
set(dwarf_reduce_target "${CMAKE_CURRENT_BINARY_DIR}/dwarf_reduce")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(HIKARI_CARGO cargo)
    find_program(HIKARI_GXX g++)
    find_program(HIKARI_OBJCOPY objcopy)
endif()
if(HIKARI_CARGO AND HIKARI_GXX AND HIKARI_OBJCOPY)
    add_test(NAME tools.dwarf-reduce
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/dwarf_reduce/ctest.sh" "${HIKARI_CARGO}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dwarf_reduce/Cargo.toml" "${dwarf_reduce_target}")
    set_tests_properties(tools.dwarf-reduce PROPERTIES LABELS "tools" TIMEOUT 600 SKIP_RETURN_CODE 77)
endif()
if(Python3_Interpreter_FOUND)
    add_test(NAME crash_tools
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py")
    set_tests_properties(crash_tools PROPERTIES LABELS "tools" TIMEOUT 60
        ENVIRONMENT "HIKARI_DWARF_REDUCE=${dwarf_reduce_target}/release/dwarf-reduce")
    if(TEST tools.dwarf-reduce)
        set_tests_properties(crash_tools PROPERTIES DEPENDS tools.dwarf-reduce)
    endif()
endif()
