# UD1 (#362): the real composition's user folder. Each case starts
# hikari_app_startup (the window opens, then the program exits) in a home of
# its own and checks the folders it leaves:
#   - fresh: settings.toml in $XDG_CONFIG_HOME/hikarisub, the bundled automation
#     seeded into automation/autoload and include from the build tree's
#     manifest, nothing in the old Qt folders;
#   - moved: a populated ~/.config/HikariSub/HikariSub and
#     ~/.local/share/HikariSub/HikariSub move in once, the moved hikari.ini
#     converted into settings.toml (CF1) and kept as hikari.ini.old;
#     legacy's folder stays;
#   - empty: empty old folders move nothing and are removed;
#   - override: HIKARISUB_USER_DIR holds everything; the old folders stay.
#
#   cmake -DSTARTUP=<hikari_app_startup> -DWORK=<dir> -P check_user_folder.cmake
foreach(var STARTUP WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

function(start case home)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=HIKARISUB_USER_DIR "HOME=${home}" "XDG_CONFIG_HOME=${home}/config"
            "XDG_DATA_HOME=${home}/data" "XDG_CACHE_HOME=${home}/cache" QT_QPA_PLATFORM=offscreen ${ARGN} "${STARTUP}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 60)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${case}: the startup exited with ${result}:\n${out}")
    endif()
endfunction()

function(expect case path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${case}: ${path} is missing")
    endif()
endfunction()

function(expect_not case path)
    if(EXISTS "${path}")
        message(FATAL_ERROR "${case}: ${path} should not be there")
    endif()
endfunction()

function(expect_text case path text)
    expect("${case}" "${path}")
    file(READ "${path}" have)
    if(NOT have STREQUAL text)
        message(FATAL_ERROR "${case}: ${path} holds \"${have}\", expected \"${text}\"")
    endif()
endfunction()

function(fresh_home case out)
    set(home "${WORK}/${case}")
    file(REMOVE_RECURSE "${home}")
    file(MAKE_DIRECTORY "${home}")
    set(${out} "${home}" PARENT_SCOPE)
endfunction()

# fresh
fresh_home(fresh home)
start(fresh "${home}")
set(user "${home}/config/hikarisub")
expect(fresh "${user}/settings.toml")
expect_not(fresh "${user}/hikari.ini")
expect(fresh "${user}/.bundled-files")
expect(fresh "${user}/automation/autoload/kara-templater.lua")
expect(fresh "${user}/automation/autoload/l0.DependencyControl.Toolbox.moon")
expect(fresh "${user}/automation/include/l0/DependencyControl.moon")
expect(fresh "${user}/automation/include/karaskel.lua")
foreach(made log temp autosave)
    expect(fresh "${user}/${made}")
endforeach()
expect_not(fresh "${home}/config/HikariSub")
expect_not(fresh "${home}/data/HikariSub")
# A changed seeded script is kept by the next start.
file(WRITE "${user}/automation/autoload/kara-templater.lua" "-- mine\n")
start(fresh-again "${home}")
expect_text(fresh-again "${user}/automation/autoload/kara-templater.lua" "-- mine\n")
message(STATUS "fresh: the user folder and its seeded automation")

# moved
fresh_home(moved home)
set(config "${home}/config/HikariSub/HikariSub")
set(data "${home}/data/HikariSub/HikariSub")
file(WRITE "${config}/hikari.ini" "[profile]\nprogram.language=pl\n")
file(WRITE "${config}/Rules.txt" "rules\n")
file(WRITE "${config}/Dictionary/UserDic.udic" "word\n")
file(WRITE "${config}/layouts.json" "{}\n")
file(WRITE "${config}/Indices/a.ffindex" "index\n")
file(WRITE "${data}/Recovery/sessions/keep.txt" "recovery\n")
file(WRITE "${data}/Automation/automation/Autoload/mine.lua" "-- mine\n")
file(WRITE "${home}/data/HikariSub/Legacy/Config/Config.txt" "legacy\n")
start(moved "${home}")
set(user "${home}/config/hikarisub")
expect_not(moved "${user}/hikari.ini")
expect_text(moved "${user}/hikari.ini.old" "[profile]\nprogram.language=pl\n")
file(STRINGS "${user}/settings.toml" settings)
if(NOT "language = 'pl'" IN_LIST settings AND NOT "language = \"pl\"" IN_LIST settings)
    message(FATAL_ERROR "moved: the converted settings lost the language:\n${settings}")
endif()
# CF2 (#489): the start converts the moved Rules.txt into rules.toml once.
expect_not(moved "${user}/Rules.txt")
expect_text(moved "${user}/Rules.txt.old" "rules\n")
file(READ "${user}/rules.toml" rules)
if(NOT rules MATCHES "^version = 1\n")
    message(FATAL_ERROR "moved: Rules.txt was not converted into rules.toml:\n${rules}")
endif()
expect_text(moved "${user}/dictionaries/UserDic.udic" "word\n")
expect_text(moved "${user}/presets/layouts.json" "{}\n")
expect_text(moved "${home}/cache/hikarisub/Indices/a.ffindex" "index\n")
expect_text(moved "${user}/recovery/sessions/keep.txt" "recovery\n")
expect_text(moved "${user}/automation/autoload/mine.lua" "-- mine\n")
expect(moved "${user}/automation/autoload/kara-templater.lua")
expect_not(moved "${config}")
expect_not(moved "${data}")
expect_not(moved "${home}/config/HikariSub")
expect_text(moved "${home}/data/HikariSub/Legacy/Config/Config.txt" "legacy\n")
# Once: an old folder that comes back later stays where it is.
file(WRITE "${config}/hikari.ini" "[General]\nprogram.language=de\n")
start(moved-again "${home}")
expect_text(moved-again "${config}/hikari.ini" "[General]\nprogram.language=de\n")
message(STATUS "moved: the old Qt folders moved in once, legacy's untouched")

# empty
fresh_home(empty home)
file(MAKE_DIRECTORY "${home}/config/HikariSub/HikariSub" "${home}/data/HikariSub/HikariSub")
start(empty "${home}")
expect(empty "${home}/config/hikarisub/settings.toml")
expect_not(empty "${home}/config/HikariSub")
expect_not(empty "${home}/data/HikariSub")
message(STATUS "empty: empty old folders removed")

# override
fresh_home(override home)
file(WRITE "${home}/config/HikariSub/HikariSub/hikari.ini" "[General]\nprogram.language=pl\n")
start(override "${home}" "HIKARISUB_USER_DIR=${home}/mine")
expect(override "${home}/mine/settings.toml")
expect(override "${home}/mine/automation/include/l0/DependencyControl.moon")
expect_not(override "${home}/config/hikarisub")
expect_text(override "${home}/config/HikariSub/HikariSub/hikari.ini" "[General]\nprogram.language=pl\n")
message(STATUS "override: HIKARISUB_USER_DIR holds the user folder")
