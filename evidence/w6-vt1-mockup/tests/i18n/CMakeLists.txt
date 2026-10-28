# O4: the PO-to-TS migration, its checked-in catalogs and their QM.
hikari_add_gtest(hikari_i18n_migration_tests SOURCES migration_tests.cpp
    LIBRARIES hikari_i18n LABELS i18n)
target_compile_definitions(hikari_i18n_migration_tests PRIVATE
    HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}" HIKARI_I18N_INPUT_COMMIT="${HIKARI_I18N_INPUT_COMMIT}")

# The legacy plural entries as Qt numerus catalogs (the "probe"), compiled
# from Locale/*.po like the catalogs, so the tests compare Qt's numerus rules
# with the legacy Plural-Forms; and, where gettext is installed, the legacy
# MO exactly as tools/compile_catalogs.py:42 builds it (msgfmt -c).
set(probe_dir "${CMAKE_CURRENT_BINARY_DIR}/probe")
set(probe_outputs "")
foreach(input IN ITEMS "pl;pl.po" "ko_KR;ko_KR.po" "th_TH;th_TH.po" "ta;ta.po" "en;template.pot")
    list(GET input 0 language)
    list(GET input 1 file)
    set(ts "${probe_dir}/legacy_plurals_${language}.ts")
    set(qm "${probe_dir}/legacy_plurals_${language}.qm")
    add_custom_command(OUTPUT "${qm}"
        COMMAND ${HIKARI_I18N_RUN} $<TARGET_FILE:hikari_i18n_migrate> probe --po "${PROJECT_SOURCE_DIR}/Locale/${file}" --language ${language} --out "${ts}"
        COMMAND Qt6::lrelease -silent "${ts}" -qm "${qm}"
        DEPENDS hikari_i18n_migrate Qt6::lrelease "${PROJECT_SOURCE_DIR}/Locale/${file}"
        VERBATIM)
    list(APPEND probe_outputs "${qm}")
endforeach()
find_program(HIKARI_MSGFMT msgfmt)
set(mo_definition "")
if(HIKARI_MSGFMT)
    foreach(language IN ITEMS pl ko_KR th_TH ta)
        set(mo "${probe_dir}/legacy_${language}.mo")
        add_custom_command(OUTPUT "${mo}"
            COMMAND "${HIKARI_MSGFMT}" -c -o "${mo}" "${PROJECT_SOURCE_DIR}/Locale/${language}.po"
            DEPENDS "${PROJECT_SOURCE_DIR}/Locale/${language}.po" VERBATIM)
        list(APPEND probe_outputs "${mo}")
    endforeach()
    set(mo_definition HIKARI_LEGACY_MO_DIR="${probe_dir}")
endif()
add_custom_target(hikari_i18n_probe DEPENDS ${probe_outputs})

hikari_add_gtest(hikari_i18n_qm_tests SOURCES qm_tests.cpp
    LIBRARIES hikari_i18n hikari_translations LABELS i18n)
add_dependencies(hikari_i18n_qm_tests hikari_i18n_probe)
target_compile_definitions(hikari_i18n_qm_tests PRIVATE
    HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}" HIKARI_PROBE_DIR="${probe_dir}"
    HIKARI_LRELEASE="$<TARGET_FILE:Qt6::lrelease>" ${mo_definition})

# Ordinary builds compile TS and never write it: every catalog still has the
# hash it had when the build was configured.
add_test(NAME hikari_i18n_build_leaves_ts
    COMMAND "${CMAKE_COMMAND}" -DHASHES=${CMAKE_BINARY_DIR}/i18n/ts-at-configure.sha256
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_ts_hashes.cmake")
set_tests_properties(hikari_i18n_build_leaves_ts PROPERTIES LABELS "i18n" TIMEOUT 60)

# Extraction reads every source with a translation call (the targets listed
# in i18n/Extraction.cmake), from the lupdate project qt_add_lupdate
# configures. Only hikari_lupdate turns that project into JSON, and it also
# rewrites the TS, so the test makes its own JSON from the configured project
# as hikari_lupdate would; a fresh build dir has no other copy.
add_test(NAME hikari_i18n_extraction_reads_every_caller
    COMMAND "${CMAKE_COMMAND}" -DPROJECT_IN=${CMAKE_BINARY_DIR}/.lupdate/hikari_lupdate_project.cmake
        -DPROJECT_JSON=${CMAKE_CURRENT_BINARY_DIR}/hikari_lupdate_project.json
        -DGENERATE_PROJECT=${Qt6LinguistTools_DIR}/GenerateLUpdateProject.cmake
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_extraction_sources.cmake")
set_tests_properties(hikari_i18n_extraction_reads_every_caller PROPERTIES LABELS "i18n" TIMEOUT 60)

# O6: the semantic contexts' lint, the record of their move and the
# checked-in registry, map and catalogs.
hikari_add_gtest(hikari_i18n_contexts_tests SOURCES contexts_tests.cpp LIBRARIES hikari_i18n LABELS i18n)
target_compile_definitions(hikari_i18n_contexts_tests PRIVATE HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# O6: every translatable string has a semantic context. The lint reads the
# same lupdate project, with locations, and i18n/contexts.tsv.
add_test(NAME hikari_i18n_contexts_are_semantic
    COMMAND "${CMAKE_COMMAND}" -DPROJECT_IN=${CMAKE_BINARY_DIR}/.lupdate/hikari_lupdate_project.cmake
        -DPROJECT_JSON=${CMAKE_CURRENT_BINARY_DIR}/hikari_contexts_project.json
        -DGENERATE_PROJECT=${Qt6LinguistTools_DIR}/GenerateLUpdateProject.cmake
        -DEXTRACTED=${CMAKE_CURRENT_BINARY_DIR}/contexts-lint/extracted.ts
        -DLUPDATE=$<TARGET_FILE:Qt6::lupdate> -DTOOL=$<TARGET_FILE:hikari_i18n_migrate>
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_contexts.cmake")
set_tests_properties(hikari_i18n_contexts_are_semantic PROPERTIES LABELS "i18n" TIMEOUT 300)

# O6: the move kept every translation: the interface catalogs before it (at
# 3e06caa9) and as it left them, by their git blobs.
find_package(Git QUIET)
set(hikari_catalogs_before_o6 pl=a003a460493dd5f63efe15b275549e71b2c6cca0 ko_KR=1ae21807cae62fbda220dc3d503d9ba2816f67c4
    th_TH=7155289308df7893efb98e135c06133d1c070b5c ta=a3b0f762e17fe913decb42b48b9c2b35269f9bf7)
set(hikari_catalogs_after_o6 pl=253204e3c0847dca1c63f07d2427a2a0eb0b9e02 ko_KR=4f6c42b6bffc59e5f6a54a3b122b009943e570ce
    th_TH=1b12c7f05a4f2be76442dbec13661e84b215d661 ta=0146b63217a74da2dfc15c787a7de9048816e200)
add_test(NAME hikari_i18n_contexts_keep_translations
    COMMAND "${CMAKE_COMMAND}" "-DBEFORE=${hikari_catalogs_before_o6}" "-DAFTER=${hikari_catalogs_after_o6}"
        -DGIT=${GIT_EXECUTABLE} -DTOOL=$<TARGET_FILE:hikari_i18n_migrate> -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/contexts-move -P "${CMAKE_CURRENT_SOURCE_DIR}/check_contexts_keep_translations.cmake")
set_tests_properties(hikari_i18n_contexts_keep_translations PROPERTIES LABELS "i18n" TIMEOUT 120
    SKIP_REGULAR_EXPRESSION "SKIP: ")
if(WIN32)
    # The tool's Qt DLLs, as HIKARI_I18N_RUN puts them first for the build.
    set_tests_properties(hikari_i18n_contexts_are_semantic hikari_i18n_contexts_keep_translations PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${HIKARI_QT_PREFIX}/bin")
endif()
