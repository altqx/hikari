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
        COMMAND hikari_i18n_migrate probe --po "${PROJECT_SOURCE_DIR}/Locale/${file}" --language ${language} --out "${ts}"
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
# in i18n/Extraction.cmake), from the lupdate project qt_add_lupdate writes.
add_test(NAME hikari_i18n_extraction_reads_every_caller
    COMMAND "${CMAKE_COMMAND}" -DPROJECT_JSON=${CMAKE_BINARY_DIR}/.lupdate/hikari_lupdate_project.json
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_extraction_sources.cmake")
set_tests_properties(hikari_i18n_extraction_reads_every_caller PROPERTIES LABELS "i18n" TIMEOUT 60)
