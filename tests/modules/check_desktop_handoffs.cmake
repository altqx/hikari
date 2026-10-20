# 38e: every hand-off to a program on the desktop goes through
# backends::desktop (src/backends/include/hikari/backends/desktop_handoff.h),
# whose routes the tests replace, so no test can reach a real browser, file
# manager or editor. This fails when a source under ${ROOT}/src opens a URL or
# starts a program another way.
#   cmake -DROOT=<tree> -P check_desktop_handoffs.cmake
#
# The exceptions, by file and count: the seam itself, and the application's
# own relaunches and installer runs after an update (not hand-offs; the update
# tests drive them with stand-in programs), and its start for the files a quit
# left (WS5).
if(NOT ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

set(pattern "QDesktopServices::openUrl|openUrlExternally|QProcess::startDetached|[.>]startDetached\\(|ShellExecute")
set(allowed
    "src/backends/desktop_handoff.cpp=2"   # QDesktopServices::openUrl, QProcess::startDetached
    "src/app/update_installer.cpp=4"       # the relaunches (3 startDetached) and the NSIS run (ShellExecuteExW)
    "src/app/composition.cpp=1")           # WS5: a start for the files a quit left (startForHandedOn)

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
            continue() # a comment
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
