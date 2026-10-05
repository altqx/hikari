# O4: the PO-to-TS migration (docs/qt/localisation.md) as a library the tests
# exercise and the developer-invoked hikari_i18n_migrate tool (i18n/CMakeLists.txt).
add_library(hikari_i18n STATIC po.cpp ts.cpp migration.cpp
    include/hikari/i18n/po.h include/hikari/i18n/ts.h include/hikari/i18n/migration.h)
target_include_directories(hikari_i18n PUBLIC include)
target_link_libraries(hikari_i18n PUBLIC Qt6::Core)

add_executable(hikari_i18n_migrate main.cpp)
target_link_libraries(hikari_i18n_migrate PRIVATE hikari_i18n)
