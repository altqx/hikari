# Composition root: constructs services once, starts the engine, shuts down in
# order. The library lets tests start the same composition without shipping a
# test hook in the executable.
add_library(hikari_composition STATIC composition.cpp application.cpp include/hikari/app/application.h)
set_target_properties(hikari_composition PROPERTIES AUTOMOC ON)
target_include_directories(hikari_composition PUBLIC include)
target_link_libraries(hikari_composition PUBLIC hikari_ui hikari_uiplugin hikari_backends hikari_application hikari_core
    Qt6::Gui Qt6::Qml Qt6::Quick)
hikari_module(hikari_composition ALLOW hikari_ui hikari_ui_resources_1 hikari_uiplugin hikari_backends
    hikari_application hikari_core "Qt6::*" "hikari_ui*")

qt_add_executable(hikarisub main.cpp)
target_link_libraries(hikarisub PRIVATE hikari_composition)
hikari_module(hikarisub ALLOW hikari_composition "Qt6::*")
