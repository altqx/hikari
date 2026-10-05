# O4: translations (ADR-0002, docs/qt/localisation.md). TS is the authoring
# source; ordinary builds compile it to QM (unfinished entries left out) and
# never write it. Extraction and the migration run only on request:
#
#   hikari_lupdate       updates hikarisub_<language>.ts from the sources
#   hikari_i18n_keymap   re-extracts the keys into migration/keys.ts and
#                        regenerates the candidate migration/keymap.tsv
#   hikari_i18n_convert  converts the legacy PO through the reviewed key map
#                        (one-shot: it rewrites every catalog here)
#
# Those need the source targets, so Extraction.cmake defines them once the
# top-level directory has added src.
#
# The gettext catalogs hold the aegisub.gettext compatibility entries; they
# are not lupdate's, so extraction can never obsolete them.
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS LinguistTools)

# The legacy catalogs the migration read (Locale/*.po), recorded in the
# catalogs and the report.
set(HIKARI_I18N_INPUT_COMMIT 20d647c4c769ab7f5d383cf3c1c33f03876a94e9)
set(HIKARI_LANGUAGES pl ko_KR th_TH ta)

set(hikari_ui_ts "")
set(hikari_gettext_ts "")
set(hikari_po_args "")
foreach(language IN LISTS HIKARI_LANGUAGES)
    list(APPEND hikari_ui_ts "${CMAKE_CURRENT_SOURCE_DIR}/hikarisub_${language}.ts")
    list(APPEND hikari_gettext_ts "${CMAKE_CURRENT_SOURCE_DIR}/hikarisub_gettext_${language}.ts")
    list(APPEND hikari_po_args --po "${language}=${PROJECT_SOURCE_DIR}/Locale/${language}.po")
endforeach()
set(HIKARI_I18N_UI_TS ${hikari_ui_ts} PARENT_SCOPE)
set(HIKARI_I18N_PO_ARGS ${hikari_po_args} PARENT_SCOPE)
set(HIKARI_I18N_INPUT_COMMIT ${HIKARI_I18N_INPUT_COMMIT} PARENT_SCOPE)

# QM in the build, embedded under :/i18n.
qt_add_lrelease(TS_FILES ${hikari_ui_ts} ${hikari_gettext_ts} QM_FILES_OUTPUT_VARIABLE hikari_qm_files
    QM_OUTPUT_DIRECTORY qm LRELEASE_TARGET hikari_lrelease NO_GLOBAL_TARGET OPTIONS -nounfinished -silent)
qt_add_library(hikari_translations STATIC)
qt_add_resources(hikari_translations hikari_translations PREFIX /i18n BASE "${CMAKE_CURRENT_BINARY_DIR}/qm"
    FILES ${hikari_qm_files} OUTPUT_TARGETS hikari_translations_resource_targets)
# The resource initializer is an object library of its own; Qt built with
# reduce_relocations needs it position independent, and Fedora's GCC module
# scan preprocesses it without the flag otherwise (#error in qcompilerdetection.h).
set_target_properties(hikari_translations ${hikari_translations_resource_targets}
    PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_dependencies(hikari_translations hikari_lrelease)

# A source edit (lupdate, a translator) reconfigures, so the recorded hashes
# are those of the last configure: the build itself must not change them
# (tests/i18n checks).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${hikari_ui_ts} ${hikari_gettext_ts})
set(hikari_ts_hashes "")
foreach(ts IN LISTS hikari_ui_ts hikari_gettext_ts)
    file(SHA256 "${ts}" hash)
    string(APPEND hikari_ts_hashes "${hash}  ${ts}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ts-at-configure.sha256" "${hikari_ts_hashes}")
