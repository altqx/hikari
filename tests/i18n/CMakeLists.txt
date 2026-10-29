hikari_add_gtest(hikari_i18n_qm_tests SOURCES qm_tests.cpp
    LIBRARIES hikari_i18n hikari_translations LABELS i18n)
target_compile_definitions(hikari_i18n_qm_tests PRIVATE
    HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}" HIKARI_LRELEASE="$<TARGET_FILE:Qt6::lrelease>")

add_test(NAME hikari_i18n_build_leaves_ts
    COMMAND "${CMAKE_COMMAND}" -DHASHES=${CMAKE_BINARY_DIR}/i18n/ts-at-configure.sha256
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_ts_hashes.cmake")
set_tests_properties(hikari_i18n_build_leaves_ts PROPERTIES LABELS "i18n" TIMEOUT 60)

# Only hikari_lupdate turns qt_add_lupdate's project into JSON, and it also rewrites the TS, so make our own.
add_test(NAME hikari_i18n_extraction_reads_every_caller
    COMMAND "${CMAKE_COMMAND}" -DPROJECT_IN=${CMAKE_BINARY_DIR}/.lupdate/hikari_lupdate_project.cmake
        -DPROJECT_JSON=${CMAKE_CURRENT_BINARY_DIR}/hikari_lupdate_project.json
        -DGENERATE_PROJECT=${Qt6LinguistTools_DIR}/GenerateLUpdateProject.cmake
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_extraction_sources.cmake")
set_tests_properties(hikari_i18n_extraction_reads_every_caller PROPERTIES LABELS "i18n" TIMEOUT 60)

hikari_add_gtest(hikari_i18n_contexts_tests SOURCES contexts_tests.cpp LIBRARIES hikari_i18n LABELS i18n)
target_compile_definitions(hikari_i18n_contexts_tests PRIVATE HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME hikari_i18n_contexts_are_semantic
    COMMAND "${CMAKE_COMMAND}" -DPROJECT_IN=${CMAKE_BINARY_DIR}/.lupdate/hikari_lupdate_project.cmake
        -DPROJECT_JSON=${CMAKE_CURRENT_BINARY_DIR}/hikari_contexts_project.json
        -DGENERATE_PROJECT=${Qt6LinguistTools_DIR}/GenerateLUpdateProject.cmake
        -DEXTRACTED=${CMAKE_CURRENT_BINARY_DIR}/contexts-lint/extracted.ts
        -DLUPDATE=$<TARGET_FILE:Qt6::lupdate> -DTOOL=$<TARGET_FILE:hikari_i18n_lint>
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_contexts.cmake")
set_tests_properties(hikari_i18n_contexts_are_semantic PROPERTIES LABELS "i18n" TIMEOUT 300)

if(WIN32)
    set_tests_properties(hikari_i18n_contexts_are_semantic PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${HIKARI_QT_PREFIX}/bin")
endif()
