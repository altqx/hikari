# UD1 (#362): bundled-files.txt in the program folder, the bundled files the
# start seeds into the user folder (src/app/include/hikari/app/user_folder.h),
# one "<SHA-256> <path>" line each, the path relative to the program folder:
# every file under Automation/automation and the Dictionary folder's own
# .dic and .aff files.
#
#   include() it and call hikari_write_bundled_manifest(<program folder>), or
#   cmake -DROOT=<program folder> -P HikariBundledManifest.cmake
function(hikari_write_bundled_manifest root)
    file(GLOB_RECURSE automation LIST_DIRECTORIES false RELATIVE "${root}" "${root}/Automation/automation/*")
    file(GLOB dictionaries LIST_DIRECTORIES false RELATIVE "${root}" "${root}/Dictionary/*.dic" "${root}/Dictionary/*.aff")
    list(SORT automation)
    list(SORT dictionaries)
    set(lines "")
    foreach(path IN LISTS automation dictionaries)
        file(SHA256 "${root}/${path}" digest)
        string(APPEND lines "${digest} ${path}\n")
    endforeach()
    # Unchanged contents keep the file's time, so nothing depending on it reruns.
    file(CONFIGURE OUTPUT "${root}/bundled-files.txt" CONTENT "${lines}" @ONLY)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DEFINED ROOT)
        message(FATAL_ERROR "HikariBundledManifest: ROOT (the program folder) is required")
    endif()
    hikari_write_bundled_manifest("${ROOT}")
endif()
