# Linux desktop integration: .desktop, AppStream metainfo, MIME icons.
#
# Only data is installed, deliberately not the executable. The runtime locates
# its resources by walking up from the binary's own directory
# (config.cpp FindResourceRoot / LoadOptions), so an exe in /usr/bin would
# never find /usr/share/hikarisub. Making HikariSub relocatable is a separate job.

option(HIKARISUB_INSTALL_DESKTOP_INTEGRATION
       "Install .desktop, AppStream metainfo, MIME and hicolor icons" ON)
set(HIKARISUB_DESKTOP_EXEC "hikarisub" CACHE STRING
    "Exec= command written into hikarisub.desktop")

# MimeType= comes from HikariSub/FileTypes.h so the two cannot drift. Rows whose
# type is nullptr (.txt, which is text/plain) are skipped on purpose.
function(hikarisub_collect_mimetypes out_var)
    file(READ "${CMAKE_SOURCE_DIR}/HikariSub/FileTypes.h" _tbl)
    string(REGEX MATCHALL "\"[a-z]+/[a-zA-Z0-9.+-]+\"" _quoted "${_tbl}")

    set(_seen "")
    foreach(_q IN LISTS _quoted)
        string(REPLACE "\"" "" _type "${_q}")
        if(NOT _type IN_LIST _seen)
            list(APPEND _seen "${_type}")
        endif()
    endforeach()

    list(LENGTH _seen _count)
    if(_count EQUAL 0)
        message(FATAL_ERROR "No mime types found in HikariSub/FileTypes.h")
    endif()

    string(JOIN ";" _joined ${_seen})
    set(${out_var} "${_joined};" PARENT_SCOPE)
endfunction()

if(HIKARISUB_INSTALL_DESKTOP_INTEGRATION)
    include(GNUInstallDirs)

    file(STRINGS "${CMAKE_SOURCE_DIR}/HikariSub/VersionHikariSub.h" _ver_line
         REGEX "^#define[ \t]+VersionHikariSub[ \t]+\"")
    string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" HIKARISUB_VERSION "${_ver_line}")
    if(NOT HIKARISUB_VERSION)
        message(FATAL_ERROR "Could not read VersionHikariSub from HikariSub/VersionHikariSub.h")
    endif()
    # A prerelease such as 1.2.0-rc.1 is a development release to AppStream.
    if(HIKARISUB_VERSION MATCHES "-")
        set(HIKARISUB_RELEASE_TYPE "development")
    else()
        set(HIKARISUB_RELEASE_TYPE "stable")
    endif()

    # AppStream requires a date on every release entry. The commit date is the
    # only one in the tree that is deterministic; GetReleaseDate() is __DATE__.
    find_package(Git QUIET)
    set(HIKARISUB_RELEASE_DATE "")
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}"
                                log -1 --format=%cs
                        OUTPUT_VARIABLE HIKARISUB_RELEASE_DATE
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(NOT HIKARISUB_RELEASE_DATE)
        set(HIKARISUB_RELEASE_DATE "1970-01-01")
    endif()

    hikarisub_collect_mimetypes(HIKARISUB_DESKTOP_MIMETYPES)
    message(STATUS "Desktop MimeType=: ${HIKARISUB_DESKTOP_MIMETYPES}")

    set(_pkg "${CMAKE_SOURCE_DIR}/packaging/linux")
    set(_gen "${CMAKE_BINARY_DIR}/packaging")

    configure_file("${_pkg}/io.github.altqx.HikariSub.desktop.in"
                   "${_gen}/io.github.altqx.HikariSub.desktop" @ONLY)
    configure_file("${_pkg}/io.github.altqx.HikariSub.metainfo.xml.in"
                   "${_gen}/io.github.altqx.HikariSub.metainfo.xml" @ONLY)

    install(FILES "${_gen}/io.github.altqx.HikariSub.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES "${_gen}/io.github.altqx.HikariSub.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
    install(FILES "${_pkg}/mime/hikarisub.xml"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/mime/packages")
    install(DIRECTORY "${_pkg}/icons/hicolor"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons"
            FILES_MATCHING PATTERN "*.png")

    # No update-desktop-database / update-mime-database hook here: distro
    # tooling runs those, and running them against DESTDIR would be wrong.
    # install-desktop-integration.sh does it for people using the tarball.
endif()
