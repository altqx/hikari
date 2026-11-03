file(READ "${CMAKE_CURRENT_SOURCE_DIR}/document.lua" HIKARI_DOCUMENT_LUA)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ui.lua" HIKARI_UI_LUA)
configure_file(preludes.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/preludes.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/document.lua" "${CMAKE_CURRENT_SOURCE_DIR}/ui.lua")

add_library(hikari_plugin_state STATIC plugin_state.cpp plugin_state.h preludes.h
    "${CMAKE_CURRENT_BINARY_DIR}/preludes.cpp")
target_include_directories(hikari_plugin_state PUBLIC .)
target_link_libraries(hikari_plugin_state PUBLIC hikari_api PRIVATE Hikari::luajit)
hikari_module(hikari_plugin_state ALLOW hikari_api hikari_application hikari_core Hikari::luajit)

add_executable(hikari_plugin_helper plugin_helper.cpp)
set_target_properties(hikari_plugin_helper PROPERTIES OUTPUT_NAME hikari-plugin-helper)
target_link_libraries(hikari_plugin_helper PRIVATE hikari_plugin_state hikari_plugin_protocol)
hikari_module(hikari_plugin_helper ALLOW hikari_plugin_state hikari_plugin_protocol hikari_helper_endpoint hikari_api
    hikari_application hikari_core)
# LuaJIT errors unwind C++ frames; MSVC's /EHsc assumes extern "C" never throws.
if(MSVC)
    target_compile_options(hikari_plugin_state PRIVATE /EHc-)
endif()
