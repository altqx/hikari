# KA4 (#279): the furigana suggestions' dictionary, UniDic 2.1.2 as NINJAL
# distributes it compiled for MeCab (the files unidic-lite packages). The
# compiled files are the same on every platform. Only what MeCab reads at
# run time is installed, with an empty mecabrc and the licence texts, into
# share/hikari-unidic-lite/dicdir; HikariSub ships that folder as
# Dictionary/unidic-lite. UniDic is GPL / LGPL / BSD at the user's choice;
# HikariSub takes the BSD licence.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(ARCHIVE
    URLS "https://clrd.ninjal.ac.jp/unidic_archive/cwj/2.1.2/unidic-mecab-2.1.2_bin.zip"
    FILENAME "unidic-mecab-2.1.2_bin.zip"
    SHA512 32646efde60fa70690b4bfdd4eddff1c0f7a2f8ce4941700f9cbb458e964f76814c6ee6ad891988c77bd9d30311f7ee27441e9b7bf02cdff8515a8cf29c1494b
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

set(dicdir "${CURRENT_PACKAGES_DIR}/share/${PORT}/dicdir")
file(INSTALL "${SOURCE_PATH}/sys.dic" "${SOURCE_PATH}/matrix.bin" "${SOURCE_PATH}/char.bin"
    "${SOURCE_PATH}/unk.dic" "${SOURCE_PATH}/dicrc" "${SOURCE_PATH}/COPYING" "${SOURCE_PATH}/BSD"
    "${SOURCE_PATH}/AUTHORS" DESTINATION "${dicdir}")
file(WRITE "${dicdir}/mecabrc" "")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING" "${SOURCE_PATH}/BSD")
