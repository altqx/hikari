# DependencyControl's native modules (ffi-experiments: BadMutex, PreciseTimer,
# DownloadManager), built from the vendored sources in Thirdparty/DependencyControl
# as the Windows solution builds them (HikariSub.sln: BadMutex, PreciseTimer and
# DownloadManager projects).  DependencyControl requires PT.PreciseTimer and
# DM.DownloadManager, and BM.BadMutex guards its config files, so without these
# DependencyControl and every script that requires it fail to load.
#
# requireffi (Automation/automation/Include/requireffi) loads "BM.BadMutex.BadMutex"
# from <package.path dir>/BM/BadMutex/libBadMutex.so, so the libraries are
# written under <output root>/BM/BadMutex, PT/PreciseTimer and DM/DownloadManager,
# the layout the Windows build writes its DLLs in.
#
#   hikarisub_add_dependencycontrol_modules(<output root>)
#
# defines hikarisub_depctrl_badmutex, hikarisub_depctrl_precisetimer and
# hikarisub_depctrl_downloadmanager, and HIKARISUB_DEPCTRL_MODULES: their paths
# relative to <output root>.
include_guard(GLOBAL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIKARISUB_DEPCTRL_CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(Threads REQUIRED)

set(HIKARISUB_DEPCTRL_MODULES
    BM/BadMutex/libBadMutex.so
    PT/PreciseTimer/libPreciseTimer.so
    DM/DownloadManager/libDownloadManager.so)

function(hikarisub_add_dependencycontrol_modules output_root)
    set(src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Thirdparty/DependencyControl")
    add_library(hikarisub_depctrl_badmutex SHARED
        "${src}/bad-mutex/BadMutex.cpp"
        "${src}/bad-mutex/BadMutexC.cpp")
    add_library(hikarisub_depctrl_precisetimer SHARED
        "${src}/precise-timer/PreciseTimer.cpp"
        "${src}/precise-timer/PreciseTimerC.cpp")
    add_library(hikarisub_depctrl_downloadmanager SHARED
        "${src}/threaded-libcurl/Downloader.cpp"
        "${src}/threaded-libcurl/DownloadManager.cpp"
        "${src}/threaded-libcurl/DownloadManagerC.cpp"
        "${src}/threaded-libcurl/sha1.c")
    target_link_libraries(hikarisub_depctrl_downloadmanager PRIVATE PkgConfig::HIKARISUB_DEPCTRL_CURL)

    foreach(module IN ITEMS BM/BadMutex PT/PreciseTimer DM/DownloadManager)
        get_filename_component(name "${module}" NAME)
        string(TOLOWER "${name}" lower)
        set(target hikarisub_depctrl_${lower})
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "${name}"
            CXX_STANDARD 17
            LIBRARY_OUTPUT_DIRECTORY "${output_root}/${module}")
        target_link_libraries(${target} PRIVATE Threads::Threads)
        # Bind each module's calls to its own functions.  The application has
        # libcurl, and through it OpenSSL's libcrypto, loaded already, and
        # libcrypto exports SHA1_Init/SHA1_Update/SHA1_Final with another
        # context layout than DownloadManager's bundled sha1.c; without this
        # its hash checks would call libcrypto's.
        target_link_options(${target} PRIVATE "-Wl,-Bsymbolic-functions")
    endforeach()
endfunction()
