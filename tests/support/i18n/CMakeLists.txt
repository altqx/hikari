# The i18n tests' TS reader and writer, and O6's semantic contexts lint as a
# library the tests exercise and the hikari_i18n_lint tool the ctest
# hikari_i18n_contexts_are_semantic runs (tests/i18n).
add_library(hikari_i18n STATIC ts.cpp contexts.cpp
    include/hikari/i18n/ts.h include/hikari/i18n/contexts.h)
target_include_directories(hikari_i18n PUBLIC include)
target_link_libraries(hikari_i18n PUBLIC Qt6::Core)

add_executable(hikari_i18n_lint main.cpp)
target_link_libraries(hikari_i18n_lint PRIVATE hikari_i18n)
