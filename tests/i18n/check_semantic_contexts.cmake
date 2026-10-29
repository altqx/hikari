# O6: every translatable string has a semantic context (i18n/contexts.tsv).
# lupdate extracts the sources hikari_lupdate reads, with locations, from the
# lupdate project qt_add_lupdate configured (as check_extraction_sources.cmake
# makes it); the lint then fails on a message whose context is not registered
# (lupdate's default: a QML file without `pragma Translator`, a C++ class's
# tr()) and on a source that names an unregistered context or calls tr(),
# QT_TR_NOOP or QT_TR_N_NOOP.
execute_process(COMMAND "${CMAKE_COMMAND}" -DIN_FILE=${PROJECT_IN} -DOUT_FILE=${PROJECT_JSON}
    -P "${GENERATE_PROJECT}" RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "could not write ${PROJECT_JSON} from ${PROJECT_IN}")
endif()
get_filename_component(extracted_dir "${EXTRACTED}" DIRECTORY)
file(MAKE_DIRECTORY "${extracted_dir}")
file(REMOVE "${EXTRACTED}")
execute_process(COMMAND "${LUPDATE}" -project "${PROJECT_JSON}" -locations absolute -no-obsolete -silent -ts "${EXTRACTED}"
    RESULT_VARIABLE extracted ERROR_VARIABLE extract_errors)
if(NOT extracted EQUAL 0 OR NOT EXISTS "${EXTRACTED}")
    message(FATAL_ERROR "lupdate could not extract the sources: ${extract_errors}")
endif()
execute_process(COMMAND "${TOOL}" --contexts "${SOURCE_DIR}/i18n/contexts.tsv" --extracted "${EXTRACTED}"
    --sources "${SOURCE_DIR}/src" RESULT_VARIABLE linted OUTPUT_VARIABLE summary ERROR_VARIABLE problems)
if(NOT linted EQUAL 0)
    message(FATAL_ERROR "${problems}")
endif()
message(STATUS "${summary}")
