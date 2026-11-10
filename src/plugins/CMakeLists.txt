set(schema "${PROJECT_SOURCE_DIR}/docs/schemas/hikari-plugin.schema.json")
set(market_schema "${PROJECT_SOURCE_DIR}/docs/schemas/marketplace-index.schema.json")
file(READ "${schema}" HIKARI_PLUGIN_SCHEMA)
file(READ "${market_schema}" HIKARI_MARKETPLACE_SCHEMA)
configure_file(manifest_schema.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/manifest_schema.cpp" @ONLY)
configure_file(marketplace_schema.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/marketplace_schema.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${schema}" "${market_schema}")

add_library(hikari_plugins STATIC manifest.cpp marketplace.cpp media.cpp package.cpp places.cpp qml_imports.cpp registry.cpp installer.cpp
    surface.cpp "${CMAKE_CURRENT_BINARY_DIR}/manifest_schema.cpp" "${CMAKE_CURRENT_BINARY_DIR}/marketplace_schema.cpp")
target_include_directories(hikari_plugins PUBLIC include)
target_link_libraries(hikari_plugins PUBLIC hikari_api hikari_minisign)
hikari_module(hikari_plugins ALLOW hikari_api hikari_minisign)
