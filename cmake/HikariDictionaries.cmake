# The spell-check dictionaries every package bundles, pinned by commit and
# SHA-256: en_US from Aegisub's dictionary repository; pl, th_TH, ko_KR and
# ta_IN from LibreOffice's (DK1 #473: ko_KR built from spellcheck-ko's
# hunspell-dict-ko, ta_IN from ThamiZha! Solthiruthi). Each dictionary's
# notice goes in licenses/dictionaries/<name>/. An entry may name a file in
# the source tree instead of a URL (a fixed copy of upstream's), checked by
# its hash as well.
#
#   hikari_fetch_dictionaries(<downloads> <out-var>)
#
# fetches every file into <downloads> (each kept while its hash matches, so an
# offline configure works from a seeded folder) and lays them out under
# <out-var>, a folder holding Dictionary/<name>.aff|.dic and
# licenses/dictionaries/<name>/<notice>, as the packages install them. Script
# mode:
#
#   cmake -DDOWNLOADS=<dir> -P cmake/HikariDictionaries.cmake
include_guard(GLOBAL)

set(_aegisub "https://raw.githubusercontent.com/TypesettingTools/Aegisub-dictionaries/6d792f04521b3ac8823c8e8972fcfd757342da4d/dicts")
set(_libreoffice "https://raw.githubusercontent.com/LibreOffice/dictionaries/32b006a2c22a4ac7e8ed3f03346f7b3d85a970a4")
set(_ko "https://raw.githubusercontent.com/spellcheck-ko/hunspell-dict-ko/606164264399ca037325bd41750f9c108ed4c290")
# The payload-relative path, the URL, the SHA-256.
set(HIKARI_DICTIONARY_FILES
    "Dictionary/en_US.aff|${_aegisub}/en_US.aff|1d1a827ce91d9eb061d5954ba325f8a538b386bd70704af431fbaac1c8e9623a"
    "Dictionary/en_US.dic|${_aegisub}/en_US.dic|b88263b1637b124d516dd3518a83a19e91dc73db6bf30d05e3aceb30e6ff4604"
    "licenses/dictionaries/en_US/README_en_US.txt|${_aegisub}/README_en_US.txt|7fa76b7fe8c639b4995ced7088f157f3cf27499f2b1fda7fd80e1928221aab5f"
    "Dictionary/pl.aff|${_libreoffice}/pl_PL/pl_PL.aff|82973651651aa930335c865b339b98db376ca3dbf3a661b70b9eeb71fdf41dca"
    "Dictionary/pl.dic|${_libreoffice}/pl_PL/pl_PL.dic|c0848440599eb88e5aca500418d5f389e562ec2c157b63dbe39d354658ffba49"
    "licenses/dictionaries/pl/README_pl_PL.txt|${_libreoffice}/pl_PL/README_pl_PL.txt|e691ed1e79934293b0cd6996a14af1520ff893c02f920c09ed0c90aa5f0a8abc"
    "Dictionary/th_TH.aff|${_libreoffice}/th_TH/th_TH.aff|a99ba46a0899a9155cd0d4f7f823ef9f5686281220ae2eb355c8b576d166e243"
    "Dictionary/th_TH.dic|${_libreoffice}/th_TH/th_TH.dic|42e9f145eb6744a3a24c498e143229a7ce7fb2cb67c8e8e94b42fde808dbf28f"
    "licenses/dictionaries/th_TH/README_th_TH.txt|${_libreoffice}/th_TH/README_th_TH.txt|87049058b4b6e4f22cf01720bcbb416dfe14bc7ca5f6ae7927821a231d559e10"
    # GPL-3.0-or-later (upstream's LICENSE.md, with the word lists' attributions).
    "Dictionary/ko_KR.aff|${_libreoffice}/ko_KR/ko_KR.aff|43a984d8f1e2309490b69cfde31832d217a0b31164c67661e293efc096083688"
    "Dictionary/ko_KR.dic|${_libreoffice}/ko_KR/ko_KR.dic|14117a72811ed6a083ed374ceb728b0846a689da6f4dbfe0a84d3b3bce124e80"
    "licenses/dictionaries/ko_KR/README_ko_KR.txt|${_libreoffice}/ko_KR/README_ko_KR.txt|e0b13b74b287a71bc668a756fe0d1bcfe5adafb860acab0d81e71593333e1724"
    "licenses/dictionaries/ko_KR/LICENSE.md|${_ko}/LICENSE.md|3e81dd90f6d8877ccb2ed2b262ce72ee8a6ccf3a21179a8949abf0d85ac7fda0"
    # MPL 1.1, as LibreOffice ships it (owner, 2026-10-29: no maintained Tamil
    # dictionary exists; Solthiruthi's upstream stopped in 2014).
    # packaging/dictionaries/ta_IN.aff is LibreOffice's (SHA-256 e05c53e6...)
    # with two header counts fixed: its SFX D table has 14 entries and SFX v
    # 5, one more than declared, and Hunspell took the extra line for a second
    # header of the flag and stopped parsing there, losing every suffix table
    # after D (owner, 2026-10-29: fix the two counts).
    "Dictionary/ta_IN.aff|packaging/dictionaries/ta_IN.aff|40e52cffa070c82d952bf93a6aba6fad6e92a658c4343cf0b66c3020a7c47573"
    "Dictionary/ta_IN.dic|${_libreoffice}/ta_IN/ta_IN.dic|4c079bde24d3232d4195e78bd33bbe5fcb887ceb6363065574cec048d04bc756"
    "licenses/dictionaries/ta_IN/COPYING|${_libreoffice}/ta_IN/COPYING|9fec7edfc79e99b210d43413ac1f309654c8c4c622ee8f51a24a3eb693f92a4e")
get_filename_component(HIKARI_DICTIONARY_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
unset(_aegisub)
unset(_libreoffice)
unset(_ko)

function(hikari_fetch_dictionaries downloads out)
    set(tree "${downloads}/tree")
    set(paths "")
    foreach(entry IN LISTS HIKARI_DICTIONARY_FILES)
        string(REPLACE "|" ";" entry "${entry}")
        list(GET entry 0 path)
        list(APPEND paths "${path}")
    endforeach()
    # A file an earlier pin laid out is not bundled any more.
    file(GLOB_RECURSE laid_out LIST_DIRECTORIES false RELATIVE "${tree}" "${tree}/*")
    foreach(path IN LISTS laid_out)
        if(NOT path IN_LIST paths)
            file(REMOVE "${tree}/${path}")
        endif()
    endforeach()
    foreach(entry IN LISTS HIKARI_DICTIONARY_FILES)
        string(REPLACE "|" ";" entry "${entry}")
        list(GET entry 0 path)
        list(GET entry 1 url)
        list(GET entry 2 sha)
        # By hash, so a changed pin never reuses another file's bytes.
        set(cached "${downloads}/${sha}")
        if(EXISTS "${cached}")
            file(SHA256 "${cached}" have)
            if(NOT have STREQUAL sha)
                file(REMOVE "${cached}")
            endif()
        endif()
        if(NOT EXISTS "${cached}" AND NOT url MATCHES "^https://")
            file(COPY_FILE "${HIKARI_DICTIONARY_SOURCE_DIR}/${url}" "${cached}")
            file(SHA256 "${cached}" have)
            if(NOT have STREQUAL sha)
                file(REMOVE "${cached}")
                message(FATAL_ERROR "Dictionaries: ${url} has SHA-256 ${have}, not ${sha}")
            endif()
        elseif(NOT EXISTS "${cached}")
            file(DOWNLOAD "${url}" "${cached}" EXPECTED_HASH SHA256=${sha} TLS_VERIFY ON STATUS status)
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                file(REMOVE "${cached}")
                message(FATAL_ERROR "Dictionaries: downloading ${url} failed: ${status}")
            endif()
        endif()
        get_filename_component(dir "${tree}/${path}" DIRECTORY)
        file(MAKE_DIRECTORY "${dir}")
        file(COPY_FILE "${cached}" "${tree}/${path}" ONLY_IF_DIFFERENT)
    endforeach()
    set(${out} "${tree}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DOWNLOADS)
        message(FATAL_ERROR "usage: cmake -DDOWNLOADS=<dir> -P ${CMAKE_CURRENT_LIST_FILE}")
    endif()
    hikari_fetch_dictionaries("${DOWNLOADS}" tree)
    message("${tree}")
endif()
