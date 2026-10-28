# The Windows payload (installers SPEC 6.5): the tree the NSIS installer and
# the portable zip ship, staged from the build by the hikari_package_windows
# target. It holds
#
#   HikariSub.exe, hikari-media-helper.exe, hikari-lua-helper.exe
#   the Qt runtime, plugins and QML imports, from windeployqt
#   the app-local MSVC runtime, from the Visual Studio redistributable folder
#   Automation/automation (the staged tree with DependencyControl's modules)
#   Dictionary/ and licenses/dictionaries/ (the dictionaries
#     HikariDictionaries.cmake pins by commit and SHA-256, and their notices)
#   Dictionary/unidic-lite/ (KA4: the furigana suggestions' UniDic 2.1.2)
#   licenses/mecab/LICENSE, licenses/unidic/LICENSE (their BSD notices)
#   Csri/VSFiltermod.dll, the prebuilt VSFilterMod release that
#     HikariVsfilterMod.cmake pins (URL and SHA-256s); the only CSRI
#     renderer the payload ships (xy-VSFilter was dropped, user 2026-10-14;
#     libass is built in and the default)
#   LICENSE
#
# and payload-manifest.txt beside the tree: every file, payload-relative with
# forward slashes, sorted. The staging fails when a direct import of any
# executable or DLL in the tree is neither in the tree nor a Windows library.
#
# Included from src/app/CMakeLists.txt on WIN32. The same file is the staging
# script (cmake -P), run by the target with the paths below.

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(HIKARI_PAYLOAD_DIR "${CMAKE_BINARY_DIR}/payload/HikariSub" CACHE PATH
        "Where hikari_package_windows stages the Windows payload")
    # The redistributable DLLs of the compiler in use (MSVC_REDIST_DIR), never
    # System32's copies. The UCRT is part of Windows 10 and later.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    set(CMAKE_INSTALL_MFC_LIBRARIES FALSE)
    set(CMAKE_INSTALL_OPENMP_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)
    set(_hikari_crt "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    if(NOT _hikari_crt)
        message(FATAL_ERROR "HikariPackageWindows: no MSVC redistributable DLLs found (MSVC_REDIST_DIR)")
    endif()
    foreach(dll IN LISTS _hikari_crt)
        if(dll MATCHES "[/\\\\][Ss]ystem32[/\\\\]")
            message(FATAL_ERROR "HikariPackageWindows: ${dll} is not from the Visual Studio redistributable folder")
        endif()
    endforeach()
    if(TARGET Qt6::windeployqt)
        set(_hikari_windeployqt "$<TARGET_FILE:Qt6::windeployqt>")
    else()
        find_program(_hikari_windeployqt_path windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" NO_DEFAULT_PATH REQUIRED)
        set(_hikari_windeployqt "${_hikari_windeployqt_path}")
    endif()
    string(REPLACE ";" "|" _hikari_crt_arg "${_hikari_crt}")
    add_custom_target(hikari_package_windows
        COMMAND "${CMAKE_COMMAND}"
            "-DSTAGE=${HIKARI_PAYLOAD_DIR}"
            "-DAPP=$<TARGET_FILE:hikarisub>"
            "-DHELPERS=$<TARGET_FILE:hikari_media_helper>|$<TARGET_FILE:hikari_lua_helper>"
            "-DAUTOMATION=${HIKARI_AUTOMATION_STAGE}"
            "-DCRT=${_hikari_crt_arg}"
            "-DWINDEPLOYQT=${_hikari_windeployqt}"
            "-DQMLDIR=${PROJECT_SOURCE_DIR}/src"
            "-DLICENSE=${PROJECT_SOURCE_DIR}/LICENSE"
            "-DUNIDIC=${HIKARI_UNIDIC_DIR}"
            "-DMECAB_LICENSE=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/mecab/copyright"
            "-DUNIDIC_LICENSE=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/hikari-unidic-lite/copyright"
            "-DDOWNLOADS=${CMAKE_BINARY_DIR}/payload-downloads"
            "-DDICTIONARIES=${HIKARI_BUNDLED_DICTIONARIES}"
            "-DHARPER=${HIKARI_HARPER_LS}"
            "-DHARPER_LICENSE=${HIKARI_HARPER_LS_LICENSE}"
            "-DHARPER_NOTICES=${PROJECT_SOURCE_DIR}/packaging/licenses/harper/THIRD-PARTY.txt"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        DEPENDS hikarisub hikari_media_helper hikari_lua_helper hikari_automation_stage
        COMMENT "Staging the Windows payload in ${HIKARI_PAYLOAD_DIR}"
        USES_TERMINAL VERBATIM)
    return()
endif()

# --- script mode -------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)
foreach(var STAGE APP HELPERS AUTOMATION CRT WINDEPLOYQT QMLDIR LICENSE DOWNLOADS DICTIONARIES UNIDIC
        MECAB_LICENSE UNIDIC_LICENSE)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "HikariPackageWindows: ${var} is required")
    endif()
endforeach()
string(REPLACE "|" ";" HELPERS "${HELPERS}")
string(REPLACE "|" ";" CRT "${CRT}")

file(REMOVE_RECURSE "${STAGE}")
file(MAKE_DIRECTORY "${STAGE}")

# The program and its helpers.
file(COPY "${APP}" ${HELPERS} DESTINATION "${STAGE}")
get_filename_component(app_name "${APP}" NAME)
if(NOT app_name STREQUAL "HikariSub.exe")
    message(FATAL_ERROR "HikariPackageWindows: the program is ${app_name}, not HikariSub.exe (SPEC 1.4)")
endif()

# The automation tree, without the build's empty log/autosave/temp folders:
# at run time they live in the user's data folders.
if(NOT IS_DIRECTORY "${AUTOMATION}")
    message(FATAL_ERROR "HikariPackageWindows: no staged automation tree at ${AUTOMATION}")
endif()
file(MAKE_DIRECTORY "${STAGE}/Automation")
file(COPY "${AUTOMATION}" DESTINATION "${STAGE}/Automation")

# The CSRI renderer: VSFilterMod only (W2; xy-VSFilter dropped, user
# 2026-10-14). No source build exists for it: the release archive and the DLL
# in it are pinned in HikariVsfilterMod.cmake.
include("${CMAKE_CURRENT_LIST_DIR}/HikariVsfilterMod.cmake")
hikari_fetch_vsfiltermod("${DOWNLOADS}" vsfiltermod_dll)
file(MAKE_DIRECTORY "${STAGE}/Csri")
file(COPY_FILE "${vsfiltermod_dll}" "${STAGE}/Csri/VSFiltermod.dll")

# DK1 (#473): the pinned dictionaries, as the build fetched them.
file(COPY "${DICTIONARIES}/Dictionary" "${DICTIONARIES}/licenses" DESTINATION "${STAGE}")

file(COPY ${CRT} DESTINATION "${STAGE}")
file(COPY_FILE "${LICENSE}" "${STAGE}/LICENSE")
# KA4 (#279): as the install rules (HikariInstall.cmake) lay them out.
file(COPY "${UNIDIC}/" DESTINATION "${STAGE}/Dictionary/unidic-lite")
file(MAKE_DIRECTORY "${STAGE}/licenses/mecab" "${STAGE}/licenses/unidic")
file(COPY_FILE "${MECAB_LICENSE}" "${STAGE}/licenses/mecab/LICENSE")
file(COPY_FILE "${UNIDIC_LICENSE}" "${STAGE}/licenses/unidic/LICENSE")
# GR1 (#364): harper-ls beside the program, with its notices.
file(COPY_FILE "${HARPER}" "${STAGE}/harper-ls.exe")
file(MAKE_DIRECTORY "${STAGE}/licenses/harper")
file(COPY_FILE "${HARPER_LICENSE}" "${STAGE}/licenses/harper/LICENSE")
file(COPY_FILE "${HARPER_NOTICES}" "${STAGE}/licenses/harper/THIRD-PARTY.txt")
# UD1 (#362): the manifest of the bundled files each start seeds into the
# user folder.
include("${CMAKE_CURRENT_LIST_DIR}/HikariBundledManifest.cmake")
hikari_write_bundled_manifest("${STAGE}")

# Qt: libraries, plugins, QML imports (scanned from every QML file under src)
# and Qt's own translations. The compiler runtime is the step above; the QML
# debugging plugins are left out of a shipped program.
execute_process(
    COMMAND "${WINDEPLOYQT}" --release --no-compiler-runtime --skip-plugin-types qmltooling --qmldir "${QMLDIR}"
        --dir "${STAGE}" "${STAGE}/HikariSub.exe"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
file(WRITE "${STAGE}/../windeployqt.log" "${out}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "HikariPackageWindows: windeployqt failed (${rc}):\n${out}")
endif()

# Every direct import of every binary in the tree resolves inside the tree or
# to a Windows library; the MSVC runtime must come from the tree.
find_program(DUMPBIN dumpbin REQUIRED)
file(GLOB_RECURSE binaries LIST_DIRECTORIES false "${STAGE}/*.exe" "${STAGE}/*.dll")
file(GLOB_RECURSE everything LIST_DIRECTORIES false RELATIVE "${STAGE}" "${STAGE}/*")
set(present "")
foreach(f IN LISTS everything)
    get_filename_component(n "${f}" NAME)
    string(TOLOWER "${n}" n)
    list(APPEND present "${n}")
endforeach()
set(system_dir "$ENV{SystemRoot}/System32")
set(problems "")
foreach(bin IN LISTS binaries)
    execute_process(COMMAND "${DUMPBIN}" /nologo /dependents "${bin}" OUTPUT_VARIABLE imports RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        list(APPEND problems "dumpbin failed on ${bin}")
        continue()
    endif()
    string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.[Dd][Ll][Ll]" dlls "${imports}")
    foreach(dll IN LISTS dlls)
        string(TOLOWER "${dll}" lower)
        if(lower IN_LIST present OR lower MATCHES "^(api|ext)-ms-win-")
            continue()
        endif()
        if(lower MATCHES "^(msvcp|vcruntime|concrt|vccorlib)[0-9]")
            list(APPEND problems "${bin} imports ${dll}, which is not in the payload")
        elseif(NOT EXISTS "${system_dir}/${dll}")
            list(APPEND problems "${bin} imports ${dll}, which is neither in the payload nor a Windows library")
        endif()
    endforeach()
endforeach()
if(problems)
    list(JOIN problems "\n  " text)
    message(FATAL_ERROR "HikariPackageWindows: unresolved imports:\n  ${text}")
endif()

# The payload's fixed members (installers SPEC 6.5; W2: the CSRI renderer;
# KA4: the furigana dictionary and the notices; GR1: harper-ls and its notices;
# DK1: every pinned dictionary and its notices).
include("${CMAKE_CURRENT_LIST_DIR}/HikariDictionaries.cmake")
set(pinned "")
foreach(entry IN LISTS HIKARI_DICTIONARY_FILES)
    string(REPLACE "|" ";" entry "${entry}")
    list(GET entry 0 path)
    list(APPEND pinned "${path}")
endforeach()
foreach(required HikariSub.exe hikari-media-helper.exe hikari-lua-helper.exe LICENSE bundled-files.txt ${pinned}
        Csri/VSFiltermod.dll Dictionary/unidic-lite/sys.dic Dictionary/unidic-lite/matrix.bin
        Dictionary/unidic-lite/char.bin Dictionary/unidic-lite/unk.dic Dictionary/unidic-lite/dicrc
        Dictionary/unidic-lite/mecabrc Dictionary/unidic-lite/BSD licenses/mecab/LICENSE licenses/unidic/LICENSE
        harper-ls.exe licenses/harper/LICENSE licenses/harper/THIRD-PARTY.txt)
    if(NOT required IN_LIST everything)
        message(FATAL_ERROR "HikariPackageWindows: the payload lacks ${required}")
    endif()
endforeach()

# The payload manifest, beside the tree.
list(SORT everything)
list(JOIN everything "\n" manifest)
get_filename_component(stage_parent "${STAGE}" DIRECTORY)
file(WRITE "${stage_parent}/payload-manifest.txt" "${manifest}\n")
list(LENGTH everything count)
message(STATUS "Windows payload: ${count} files in ${STAGE}")
