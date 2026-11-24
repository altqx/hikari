hikari_add_gtest(hikari_themes_tests SOURCES theme_tests.cpp LIBRARIES hikari_themes hikari_api LABELS themes)
target_compile_definitions(hikari_themes_tests PRIVATE HIKARI_BUILTIN_THEMES="${PROJECT_SOURCE_DIR}/src/ui/themes"
    HIKARI_THEME_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/contrast-vectors.json")
