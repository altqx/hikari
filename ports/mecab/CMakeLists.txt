# libmecab only: the analyser HikariSub links. The command-line tools, the
# dictionary compiler's mains and the bindings are not built.
cmake_minimum_required(VERSION 3.21)
project(mecab VERSION 0.996 LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(mecab
    char_property.cpp connector.cpp context_id.cpp dictionary.cpp dictionary_compiler.cpp
    dictionary_generator.cpp dictionary_rewriter.cpp eval.cpp feature_index.cpp iconv_utils.cpp
    lbfgs.cpp learner.cpp learner_tagger.cpp libmecab.cpp nbest_generator.cpp param.cpp
    string_buffer.cpp tagger.cpp tokenizer.cpp utils.cpp viterbi.cpp writer.cpp)
target_include_directories(mecab PUBLIC $<INSTALL_INTERFACE:include>)
target_link_libraries(mecab PRIVATE Threads::Threads)
target_compile_definitions(mecab PRIVATE DIC_VERSION=102 VERSION="${PROJECT_VERSION}" PACKAGE="mecab"
    HAVE_STRING_H HAVE_SYS_TYPES_H HAVE_SYS_STAT_H HAVE_FCNTL_H HAVE_STDINT_H)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(mecab PRIVATE DLL_EXPORT)
else()
    target_compile_definitions(mecab PUBLIC MECAB_STATIC)
endif()
if(WIN32)
    # Wide paths through Utf8ToWide (hikari-windows-static.patch).
    target_compile_definitions(mecab PRIVATE HAVE_WINDOWS_H HAVE_GETENV MECAB_USE_THREAD UNICODE _UNICODE
        _CRT_SECURE_NO_DEPRECATE MECAB_DEFAULT_RC="mecabrc")
else()
    # The dictionary is memory-mapped: upstream's port left HAVE_MMAP out and
    # read the whole sys.dic into the heap.
    target_compile_definitions(mecab PRIVATE HAVE_UNISTD_H HAVE_DIRENT_H HAVE_MMAP HAVE_SYS_MMAN_H
        HAVE_PTHREAD_H HAVE_SCHED_H MECAB_DEFAULT_RC="/dev/null")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS mecab EXPORT mecabTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES mecab.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT mecabTargets NAMESPACE mecab:: DESTINATION share/mecab)
file(WRITE "${PROJECT_BINARY_DIR}/mecabConfig.cmake.in" [=[
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(Threads)
include("${CMAKE_CURRENT_LIST_DIR}/mecabTargets.cmake")
check_required_components(mecab)
]=])
configure_package_config_file("${PROJECT_BINARY_DIR}/mecabConfig.cmake.in" "${PROJECT_BINARY_DIR}/mecabConfig.cmake"
    INSTALL_DESTINATION share/mecab)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/mecabConfigVersion.cmake" COMPATIBILITY SameMajorVersion)
install(FILES "${PROJECT_BINARY_DIR}/mecabConfig.cmake" "${PROJECT_BINARY_DIR}/mecabConfigVersion.cmake"
    DESTINATION share/mecab)
