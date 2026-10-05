# O4: the PO-to-TS migration (docs/qt/localisation.md) as a library the tests
# exercise and the developer-invoked hikari_i18n_migrate tool (i18n/CMakeLists.txt).
add_library(hikari_i18n STATIC po.cpp ts.cpp migration.cpp
    include/hikari/i18n/po.h include/hikari/i18n/ts.h include/hikari/i18n/migration.h)
target_include_directories(hikari_i18n PUBLIC include)
target_link_libraries(hikari_i18n PUBLIC Qt6::Core)

add_executable(hikari_i18n_migrate main.cpp)
target_link_libraries(hikari_i18n_migrate PRIVATE hikari_i18n)

# Build-time runs of the tool: on Windows its Qt DLLs are not on PATH while
# the build runs (STATUS_DLL_NOT_FOUND), so they go first for the command only,
# as the dependency-load test does.
set(HIKARI_I18N_RUN "")
if(WIN32)
    set(HIKARI_I18N_RUN "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:${HIKARI_QT_PREFIX}/bin" --)
endif()
set(HIKARI_I18N_RUN "${HIKARI_I18N_RUN}" PARENT_SCOPE)
