#   cmake -DROOT=<tree> -P check_desktop_handoffs.cmake
if(NOT ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

set(pattern "QDesktopServices::openUrl|openUrlExternally|QProcess::startDetached|[.>]startDetached\\(|ShellExecute")
set(allowed
    "src/backends/desktop_handoff.cpp=2"
    "src/app/update_installer.cpp=4"
    "src/app/composition.cpp=1")

file(GLOB_RECURSE sources RELATIVE "${ROOT}" "${ROOT}/src/*.cpp" "${ROOT}/src/*.h" "${ROOT}/src/*.qml"
    "${ROOT}/src/*.js" "${ROOT}/src/*.mjs")
set(failures "")
foreach(source IN LISTS sources)
    file(STRINGS "${ROOT}/${source}" lines REGEX "${pattern}")
    set(count 0)
    set(found "")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" stripped)
        if(stripped MATCHES "^(//|\\*|/\\*)")
            continue()
        endif()
        math(EXPR count "${count} + 1")
        string(APPEND found "\n    ${stripped}")
    endforeach()
    if(count EQUAL 0)
        continue()
    endif()
    set(expected 0)
    foreach(entry IN LISTS allowed)
        if(entry MATCHES "^([^=]+)=([0-9]+)$" AND CMAKE_MATCH_1 STREQUAL source)
            set(expected ${CMAKE_MATCH_2})
        endif()
    endforeach()
    if(NOT count EQUAL expected)
        string(APPEND failures "\n  ${source}: ${count} (allowed ${expected}):${found}")
    endif()
endforeach()
if(failures)
    message(FATAL_ERROR "Hand-offs to the desktop past backends::desktop (desktop_handoff.h), "
        "which tests cannot stand in for:${failures}")
endif()
message(STATUS "Every hand-off to the desktop goes through backends::desktop")
