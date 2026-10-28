if(NOT DEFINED SOURCE_LOCALE_DIR OR NOT DEFINED RUNTIME_LOCALE_DIR)
    message(FATAL_ERROR "SOURCE_LOCALE_DIR and RUNTIME_LOCALE_DIR are required")
endif()

if(NOT DEFINED HIKARISUB_SOURCE_DIR)
    set(HIKARISUB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

# tools/compile_catalogs.py is the one implementation; the packaging script and
# the Visual Studio pre-build event run the same file.
set(_hikarisub_msgfmt_arg)
if(MSGFMT_EXECUTABLE)
    set(_hikarisub_msgfmt_arg --msgfmt "${MSGFMT_EXECUTABLE}")
endif()

set(_hikarisub_strict_arg)
if(HIKARISUB_STRICT_LOCALES)
    set(_hikarisub_strict_arg --strict)
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${HIKARISUB_SOURCE_DIR}/tools/compile_catalogs.py"
            --po-dir "${SOURCE_LOCALE_DIR}"
            --out-dir "${RUNTIME_LOCALE_DIR}"
            ${_hikarisub_msgfmt_arg}
            ${_hikarisub_strict_arg}
    RESULT_VARIABLE _hikarisub_locale_result
)
if(_hikarisub_locale_result)
    message(FATAL_ERROR "compiling translation catalogs failed (exit ${_hikarisub_locale_result})")
endif()
