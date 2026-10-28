# Core behavior tests. Qt Core is used here only to read JSON fixtures; the
# core library itself stays Qt-free (enforced by HikariModules).
hikari_add_gtest(hikari_core_time_tests SOURCES time_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_time_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures"
    HIKARI_LEGACY_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-36591631319/observations.json")

hikari_add_gtest(hikari_core_ass_load_tests SOURCES ass_load_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_ass_load_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")

hikari_add_gtest(hikari_core_ass_save_tests SOURCES ass_save_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_ass_save_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures"
    HIKARI_LEGACY_OUTPUTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-36591631319/outputs")

# WS2: the [Hikari] section (the tab's panel arrangement in ASS files).
hikari_add_gtest(hikari_core_hikari_section_tests SOURCES hikari_section_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_hikari_section_tests PRIVATE
    HIKARI_SECTION_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/hikari-section")

hikari_add_gtest(hikari_core_srt_tests SOURCES srt_tests.cpp LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_srt_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures"
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs"
    HIKARI_CRLF_OBSERVATION="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-37011681370/outputs/srt-crlf-blank-lines/saved-crlf-two-cues.srt")

hikari_add_gtest(hikari_core_line_format_tests SOURCES line_formats_tests.cpp LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_line_format_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")

hikari_add_gtest(hikari_core_style_tests SOURCES style_tests.cpp LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_style_tests PRIVATE
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs"
    HIKARI_C4SSA_OUTPUTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-37014681128/outputs/c4-ssa-edge-families")

hikari_add_gtest(hikari_core_text_projection_tests SOURCES text_projection_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)

hikari_add_gtest(hikari_core_tag_transfer_tests SOURCES tag_transfer_tests.cpp LIBRARIES hikari_core LABELS core)
# GR1 (#364): what harper-ls is given for a Line, and Harper's rule groups.
hikari_add_gtest(hikari_core_grammar_tests SOURCES grammar_tests.cpp LIBRARIES hikari_core LABELS core)

hikari_add_gtest(hikari_core_tag_commands_tests SOURCES tag_commands_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_tag_list_tests SOURCES tag_list_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_editor_font_colour_tests SOURCES editor_font_colour_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_clipboard_rows_tests SOURCES clipboard_rows_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_conversion_tests SOURCES conversion_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_legacy_regex_tests SOURCES legacy_regex_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_spelling_tests SOURCES spelling_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_spelling_scripts_tests SOURCES spelling_scripts_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_editor_syntax_tests SOURCES editor_syntax_tests.cpp LIBRARIES hikari_core LABELS core)
# Y9: subtitles read from a Matroska track (legacy Demux).
hikari_add_gtest(hikari_core_matroska_tests SOURCES matroska_tests.cpp LIBRARIES hikari_core LABELS core)
# KA6: word-timed LRC and TTML lyrics imported as karaoke Lines.
hikari_add_gtest(hikari_core_lyrics_import_tests SOURCES lyrics_import_tests.cpp LIBRARIES hikari_core LABELS core)
target_compile_definitions(hikari_core_lyrics_import_tests PRIVATE
    HIKARI_LYRICS_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/lyrics")
# KA7: karaoke checks.
hikari_add_gtest(hikari_core_karaoke_checks_tests SOURCES karaoke_checks_tests.cpp LIBRARIES hikari_core LABELS core)
target_compile_definitions(hikari_core_karaoke_checks_tests PRIVATE
    HIKARI_KARAOKE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/karaoke/checks.ass")

# Y7: the colour picker's conversions.
hikari_add_gtest(hikari_core_colour_space_tests SOURCES colour_space_tests.cpp LIBRARIES hikari_core LABELS core)

# 38c: a script's attached fonts for the renderers, and the renderer script
# without [Fonts].
hikari_add_gtest(hikari_core_ass_attachments_tests SOURCES ass_attachments_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_ass_attachments_tests PRIVATE
    HIKARI_ATTACHMENT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/attachments")
