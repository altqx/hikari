find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS LinguistTools)

set(HIKARI_LANGUAGES pl ko_KR th_TH ta)

set(hikari_ui_ts "")
set(hikari_gettext_ts "")
foreach(language IN LISTS HIKARI_LANGUAGES)
    list(APPEND hikari_ui_ts "${CMAKE_CURRENT_SOURCE_DIR}/hikarisub_${language}.ts")
    list(APPEND hikari_gettext_ts "${CMAKE_CURRENT_SOURCE_DIR}/hikarisub_gettext_${language}.ts")
endforeach()
set(HIKARI_I18N_UI_TS ${hikari_ui_ts} PARENT_SCOPE)
set(hikari_en_ts "${CMAKE_CURRENT_SOURCE_DIR}/hikarisub_en.ts")
set(HIKARI_I18N_EN_TS ${hikari_en_ts} PARENT_SCOPE)

qt_add_lrelease(TS_FILES ${hikari_ui_ts} ${hikari_gettext_ts} ${hikari_en_ts} QM_FILES_OUTPUT_VARIABLE hikari_qm_files
    QM_OUTPUT_DIRECTORY qm LRELEASE_TARGET hikari_lrelease NO_GLOBAL_TARGET OPTIONS -nounfinished -silent)
qt_add_library(hikari_translations STATIC)
qt_add_resources(hikari_translations hikari_translations PREFIX /i18n BASE "${CMAKE_CURRENT_BINARY_DIR}/qm"
    FILES ${hikari_qm_files} OUTPUT_TARGETS hikari_translations_resource_targets)
# Qt's reduce_relocations needs the resource object library PIC (#error in qcompilerdetection.h on Fedora).
set_target_properties(hikari_translations ${hikari_translations_resource_targets}
    PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_dependencies(hikari_translations hikari_lrelease)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${hikari_ui_ts} ${hikari_gettext_ts} ${hikari_en_ts})
set(hikari_ts_hashes "")
foreach(ts IN LISTS hikari_ui_ts hikari_gettext_ts hikari_en_ts)
    file(SHA256 "${ts}" hash)
    string(APPEND hikari_ts_hashes "${hash}  ${ts}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ts-at-configure.sha256" "${hikari_ts_hashes}")
