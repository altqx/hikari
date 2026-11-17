set(schema "${PROJECT_SOURCE_DIR}/docs/schemas/hikari-theme.schema.json")
file(READ "${schema}" HIKARI_THEME_SCHEMA)
configure_file(schema.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/schema.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${schema}")

add_library(hikari_themes STATIC theme.cpp "${CMAKE_CURRENT_BINARY_DIR}/schema.cpp")
target_include_directories(hikari_themes PUBLIC include)
target_link_libraries(hikari_themes PRIVATE hikari_api)
hikari_module(hikari_themes ALLOW hikari_api)
