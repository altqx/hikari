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
# A Lua error unwinds the C++ frames between lua_error and the protecting
# pcall (LuaJIT's full C++ interoperability, which runs their destructors).
# MSVC's default /EHsc assumes extern "C" functions such as lua_error never
# throw, so a frame raising one while it holds an object with a destructor
# has no unwind state for it and ends the process; /EHs (with /EHc-) keeps it.
if(MSVC)
    target_compile_options(hikari_lua_helper PRIVATE /EHc-)
endif()
hikari_module(hikari_lua_helper ALLOW hikari_lua_protocol hikari_helper_endpoint hikari_application hikari_core
    hikari_lua_legacy_native Hikari::luajit "Boost::*" "ICU::*")

# DependencyControl's native modules (L6), built from the unchanged
# Thirdparty/DependencyControl sources as the legacy package did. requireffi
# finds each one under an include directory: BM/BadMutex/, PT/PreciseTimer/
# and DM/DownloadManager/ (lib<Name>.so, or <Name>.dll on Windows).
find_package(CURL CONFIG REQUIRED)
set(_hikari_depctrl "${PROJECT_SOURCE_DIR}/Thirdparty/DependencyControl")
add_library(hikari_depctrl_badmutex SHARED "${_hikari_depctrl}/bad-mutex/BadMutex.cpp" "${_hikari_depctrl}/bad-mutex/BadMutexC.cpp")
add_library(hikari_depctrl_precisetimer SHARED "${_hikari_depctrl}/precise-timer/PreciseTimer.cpp"
    "${_hikari_depctrl}/precise-timer/PreciseTimerC.cpp")
add_library(hikari_depctrl_downloadmanager SHARED "${_hikari_depctrl}/threaded-libcurl/Downloader.cpp"
    "${_hikari_depctrl}/threaded-libcurl/DownloadManager.cpp" "${_hikari_depctrl}/threaded-libcurl/DownloadManagerC.cpp"
    "${_hikari_depctrl}/threaded-libcurl/sha1.c")
target_link_libraries(hikari_depctrl_downloadmanager PRIVATE CURL::libcurl)
if(WIN32)
    # DownloadManager::isInternetConnected uses InternetGetConnectedState.
    target_link_libraries(hikari_depctrl_downloadmanager PRIVATE wininet)
endif()
# The bundled sha1.c names its functions SHA1_*, as OpenSSL's libcrypto does;
# with curl linked statically over OpenSSL (Linux) they would collide.
target_compile_definitions(hikari_depctrl_downloadmanager PRIVATE SHA1_Transform=hikari_dm_SHA1_Transform
    SHA1_Init=hikari_dm_SHA1_Init SHA1_Update=hikari_dm_SHA1_Update SHA1_Final=hikari_dm_SHA1_Final)
foreach(name BadMutex PreciseTimer DownloadManager)
    string(TOLOWER "${name}" lower)
    set_target_properties(hikari_depctrl_${lower} PROPERTIES OUTPUT_NAME ${name} CXX_STANDARD 17)
endforeach()

# A staged copy of the bundled automation tree with the modules in place, for
# the helper's tests and the corpus (the source tree stays untouched).
# The legacy install layout: <app>/Automation/automation/{Autoload,Include},
# with log, autosave and temp beside it (where ?user and ?data resolve).
set(HIKARI_AUTOMATION_ROOT "${CMAKE_BINARY_DIR}/Automation" CACHE INTERNAL "")
set(HIKARI_AUTOMATION_STAGE "${HIKARI_AUTOMATION_ROOT}/automation" CACHE INTERNAL "")
add_custom_target(hikari_automation_stage ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Automation/automation" "${HIKARI_AUTOMATION_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_AUTOMATION_ROOT}/log" "${HIKARI_AUTOMATION_ROOT}/autosave"
        "${HIKARI_AUTOMATION_ROOT}/temp" "${HIKARI_AUTOMATION_STAGE}/Include/BM/BadMutex"
        "${HIKARI_AUTOMATION_STAGE}/Include/PT/PreciseTimer" "${HIKARI_AUTOMATION_STAGE}/Include/DM/DownloadManager"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:hikari_depctrl_badmutex>" "${HIKARI_AUTOMATION_STAGE}/Include/BM/BadMutex/"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:hikari_depctrl_precisetimer>" "${HIKARI_AUTOMATION_STAGE}/Include/PT/PreciseTimer/"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:hikari_depctrl_downloadmanager>" "${HIKARI_AUTOMATION_STAGE}/Include/DM/DownloadManager/"
    DEPENDS hikari_depctrl_badmutex hikari_depctrl_precisetimer hikari_depctrl_downloadmanager
    COMMENT "Staging the automation tree with DependencyControl's native modules")
