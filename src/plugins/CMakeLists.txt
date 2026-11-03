set(schema "${PROJECT_SOURCE_DIR}/docs/schemas/hikari-plugin.schema.json")
file(READ "${schema}" HIKARI_PLUGIN_SCHEMA)
configure_file(manifest_schema.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/manifest_schema.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${schema}")

add_library(hikari_plugins STATIC manifest.cpp media.cpp package.cpp registry.cpp installer.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/manifest_schema.cpp")
target_include_directories(hikari_plugins PUBLIC include)
target_link_libraries(hikari_plugins PUBLIC hikari_api hikari_minisign)
hikari_module(hikari_plugins ALLOW hikari_api hikari_minisign)
