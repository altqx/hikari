# Generates the .desktop entry and the AppStream metainfo into <build>/packaging/ from packaging/linux/*.in.
include_guard(GLOBAL)

set(HIKARI_DESKTOP_EXEC "hikarisub" CACHE STRING "Exec= command written into the .desktop entry")

function(hikari_collect_mimetypes out_var)
    file(READ "${PROJECT_SOURCE_DIR}/packaging/linux/mime/hikarisub.xml" _xml)
    string(REGEX MATCHALL "<mime-type type=\"[a-z]+/[a-zA-Z0-9.+-]+\"" _tags "${_xml}")
    set(_seen "")
    foreach(_t IN LISTS _tags)
        string(REGEX REPLACE "^<mime-type type=\"|\"$" "" _type "${_t}")
        if(NOT _type IN_LIST _seen)
            list(APPEND _seen "${_type}")
        endif()
    endforeach()
    if(NOT _seen)
        message(FATAL_ERROR "No mime types found in packaging/linux/mime/hikarisub.xml")
    endif()
    string(JOIN ";" _joined ${_seen})
    set(${out_var} "${_joined};" PARENT_SCOPE)
endfunction()

# hikari_generate_desktop_files(<version>): writes <build>/packaging/io.github.altqx.HikariSub.{desktop,metainfo.xml}.
function(hikari_generate_desktop_files version)
    # The templates are shared with the legacy branch and use its variable names.
    set(HIKARISUB_VERSION "${version}")
    set(HIKARISUB_DESKTOP_EXEC "${HIKARI_DESKTOP_EXEC}")
    # A prerelease such as 0.1.0-beta.2 is a development release to AppStream.
    if(version MATCHES "-")
        set(HIKARISUB_RELEASE_TYPE "development")
    else()
        set(HIKARISUB_RELEASE_TYPE "stable")
    endif()
    # AppStream requires a date on every release entry; the commit date is the only deterministic one.
    find_package(Git QUIET)
    set(HIKARISUB_RELEASE_DATE "")
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" log -1 --format=%cs
            OUTPUT_VARIABLE HIKARISUB_RELEASE_DATE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(NOT HIKARISUB_RELEASE_DATE)
        set(HIKARISUB_RELEASE_DATE "1970-01-01")
    endif()
    hikari_collect_mimetypes(HIKARISUB_DESKTOP_MIMETYPES)

    set(_pkg "${PROJECT_SOURCE_DIR}/packaging/linux")
    set(_gen "${CMAKE_BINARY_DIR}/packaging")
    configure_file("${_pkg}/io.github.altqx.HikariSub.desktop.in" "${_gen}/io.github.altqx.HikariSub.desktop" @ONLY)
    configure_file("${_pkg}/io.github.altqx.HikariSub.metainfo.xml.in" "${_gen}/io.github.altqx.HikariSub.metainfo.xml"
        @ONLY)
endfunction()
