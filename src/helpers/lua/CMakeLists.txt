# The isolated Lua automation helper: one process per loaded script (N8; ADR 0006).
# The legacy native preloads (L6) build from the unchanged legacy sources:
# lpeg 0.10 (HikariSub/AutomationLPeg.c) and luabins (Thirdparty/luabins).
find_package(Boost CONFIG REQUIRED COMPONENTS regex locale)
find_package(ICU REQUIRED COMPONENTS uc i18n data)
add_library(hikari_lua_legacy_native STATIC
    "${PROJECT_SOURCE_DIR}/HikariSub/AutomationLPeg.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/fwrite.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/load.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/luabins.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/luainternals.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/save.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/savebuffer.c"
    "${PROJECT_SOURCE_DIR}/Thirdparty/luabins/src/write.c")
target_include_directories(hikari_lua_legacy_native PRIVATE "${PROJECT_SOURCE_DIR}/HikariSub")
target_link_libraries(hikari_lua_legacy_native PRIVATE Hikari::luajit)
set_target_properties(hikari_lua_legacy_native PROPERTIES LINKER_LANGUAGE C)

add_executable(hikari_lua_helper lua_helper.cpp lua_native.cpp lua_native.h)
set_target_properties(hikari_lua_helper PROPERTIES OUTPUT_NAME hikari-lua-helper)
# ICU is linked statically: its headers must not declare dllimport.
target_compile_definitions(hikari_lua_helper PRIVATE BOOST_ALL_NO_LIB U_STATIC_IMPLEMENTATION)
target_link_libraries(hikari_lua_helper PRIVATE hikari_lua_protocol hikari_lua_legacy_native Hikari::luajit
    Boost::regex Boost::locale ICU::i18n ICU::uc ICU::data)
hikari_module(hikari_lua_helper ALLOW hikari_lua_protocol hikari_helper_endpoint hikari_application hikari_core
    hikari_lua_legacy_native Hikari::luajit "Boost::*" "ICU::*")
