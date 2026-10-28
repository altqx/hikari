# O5-first-start-ui-languages: the real composition hands Application
# QLocale::system().uiLanguages(), so a first start whose first system UI
# language is Polish stores PROGRAM_LANGUAGE "pl" (hikarisubApp.cpp:319-325,
# where legacy read GetSystemDefaultUILanguage on Windows and
# wxLocale::GetSystemLanguage elsewhere). On Linux Qt reads the system UI
# languages from LANGUAGE (then LC_ALL, LC_MESSAGES, LANG).
#
#   cmake -DSTARTUP=<hikari_app_startup> -DWORK=<dir> -P check_first_start_language.cmake
foreach(var STARTUP WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

function(first_start case languages expected)
    set(home "${WORK}/${case}")
    file(REMOVE_RECURSE "${home}")
    file(MAKE_DIRECTORY "${home}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=LC_ALL --unset=LC_MESSAGES "LANGUAGE=${languages}" LANG=C.UTF-8
            "HOME=${home}" "XDG_CONFIG_HOME=${home}/config" "XDG_DATA_HOME=${home}/data"
            "XDG_CACHE_HOME=${home}/cache" QT_QPA_PLATFORM=offscreen "${STARTUP}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 60)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${case}: the startup exited with ${result}:\n${out}")
    endif()
    file(GLOB_RECURSE ini "${home}/config/*hikari.ini")
    if(NOT ini)
        message(FATAL_ERROR "${case}: no settings file was written under ${home}/config")
    endif()
    # The settings store's keys (O1): program.language and the spell
    # checker's editor.dictionaryLanguage, both taken from the first start.
    file(STRINGS "${ini}" all)
    set(stored "")
    set(dictionary "")
    foreach(line IN LISTS all)
        if(line MATCHES "^program\\.language=(.*)$")
            set(stored "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^editor\\.dictionaryLanguage=(.*)$")
            set(dictionary "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(expected STREQUAL "pl" AND NOT dictionary STREQUAL "pl")
        message(FATAL_ERROR "${case}: the spell checker language is \"${dictionary}\", expected \"pl\"\n${ini}")
    endif()
    if(NOT stored STREQUAL expected)
        message(FATAL_ERROR "${case}: LANGUAGE=${languages} stored program language \"${stored}\", expected \"${expected}\"\n${ini}")
    endif()
    message(STATUS "${case}: LANGUAGE=${languages} -> program language \"${stored}\"")
endfunction()

first_start(polish "pl_PL:en_US" "pl")
first_start(english-first "en_US:pl_PL" "")
