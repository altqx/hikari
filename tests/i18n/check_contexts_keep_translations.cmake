# O6: the move to semantic contexts kept every translation. The interface
# catalogs before the move (BEFORE: language=blob, the catalogs at 3e06caa9)
# and after it (AFTER: language=blob, as O6 committed them) are read from the
# repository's objects, so the check is the move's and later translation work
# does not touch it; hikari_i18n_migrate compare maps each entry before
# through i18n/migration/contexts.tsv and fails on an entry lost, changed or
# added. A checkout without the objects (a shallow clone) skips, naming them.
if(NOT GIT)
    message("SKIP: git is not installed, so the catalogs before and after O6 cannot be read")
    return()
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")
foreach(side IN ITEMS BEFORE AFTER)
    foreach(entry IN LISTS ${side})
        string(REPLACE "=" ";" entry "${entry}")
        list(GET entry 0 language)
        list(GET entry 1 blob)
        execute_process(COMMAND "${GIT}" -C "${SOURCE_DIR}" cat-file blob ${blob}
            OUTPUT_FILE "${WORK_DIR}/${side}_${language}.ts" RESULT_VARIABLE read ERROR_QUIET)
        if(NOT read EQUAL 0)
            message("SKIP: this checkout has no git object ${blob} (i18n/hikarisub_${language}.ts ${side} O6); "
                "a shallow clone holds only its own commit")
            return()
        endif()
    endforeach()
endforeach()
set(failed "")
foreach(entry IN LISTS BEFORE)
    string(REPLACE "=" ";" entry "${entry}")
    list(GET entry 0 language)
    execute_process(COMMAND "${TOOL}" compare --map "${SOURCE_DIR}/i18n/migration/contexts.tsv"
        --before "${WORK_DIR}/BEFORE_${language}.ts" --after "${WORK_DIR}/AFTER_${language}.ts"
        RESULT_VARIABLE compared OUTPUT_VARIABLE summary ERROR_VARIABLE problems)
    message(STATUS "${summary}")
    if(NOT compared EQUAL 0)
        message("${problems}")
        list(APPEND failed ${language})
    endif()
endforeach()
if(failed)
    message(FATAL_ERROR "the move lost, changed or added translated entries in: ${failed}")
endif()
