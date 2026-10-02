# The isolated Lua automation helper: one process per loaded script (N8; ADR 0006).
add_executable(hikari_lua_helper lua_helper.cpp)
set_target_properties(hikari_lua_helper PROPERTIES OUTPUT_NAME hikari-lua-helper)
target_link_libraries(hikari_lua_helper PRIVATE hikari_lua_protocol Hikari::luajit)
hikari_module(hikari_lua_helper ALLOW hikari_lua_protocol hikari_helper_endpoint hikari_application hikari_core
    Hikari::luajit)
