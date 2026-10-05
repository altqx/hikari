# O4: the developer-invoked extraction and migration targets (see
# i18n/CMakeLists.txt), included by the top-level CMakeLists.txt after src so
# the source targets exist.
# Every target with translation calls: the i18n test
# hikari_i18n_extraction_reads_every_caller fails when a source under src/
# calls qsTr/tr and none of these targets holds it.
set(HIKARI_I18N_SOURCE_TARGETS hikarisub hikari_composition hikari_ui)
set(hikari_i18n_dir "${CMAKE_CURRENT_LIST_DIR}")
qt_add_lupdate(SOURCE_TARGETS ${HIKARI_I18N_SOURCE_TARGETS} TS_FILES ${HIKARI_I18N_UI_TS}
    LUPDATE_TARGET hikari_lupdate NO_GLOBAL_TARGET OPTIONS -locations none)

set(hikari_keys_ts "${CMAKE_BINARY_DIR}/i18n/keys/hikarisub_keys.ts")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/i18n/keys")
qt_add_lupdate(SOURCE_TARGETS ${HIKARI_I18N_SOURCE_TARGETS} TS_FILES "${hikari_keys_ts}"
    LUPDATE_TARGET hikari_i18n_keys NO_GLOBAL_TARGET OPTIONS -locations none -no-obsolete)
add_custom_target(hikari_i18n_keymap
    COMMAND "${CMAKE_COMMAND}" -E copy "${hikari_keys_ts}" "${hikari_i18n_dir}/migration/keys.ts"
    COMMAND hikari_i18n_migrate keymap --pot "${PROJECT_SOURCE_DIR}/Locale/template.pot"
        --keys "${hikari_i18n_dir}/migration/keys.ts" --commit ${HIKARI_I18N_INPUT_COMMIT}
        --out "${hikari_i18n_dir}/migration/keymap.tsv"
    VERBATIM)
add_dependencies(hikari_i18n_keymap hikari_i18n_keys hikari_i18n_migrate)
add_custom_target(hikari_i18n_convert
    COMMAND hikari_i18n_migrate convert --map "${hikari_i18n_dir}/migration/keymap.tsv"
        --keys "${hikari_i18n_dir}/migration/keys.ts" --pot "${PROJECT_SOURCE_DIR}/Locale/template.pot"
        ${HIKARI_I18N_PO_ARGS} --out-dir "${hikari_i18n_dir}" --report "${hikari_i18n_dir}/migration/report.md"
    VERBATIM)
add_dependencies(hikari_i18n_convert hikari_i18n_migrate)

