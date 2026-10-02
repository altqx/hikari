# Application services: Documents, commands, history, settings and ports.
add_library(hikari_application STATIC module.cpp write_coordinator.cpp edit_session.cpp)
target_include_directories(hikari_application PUBLIC include)
target_link_libraries(hikari_application PUBLIC hikari_core)
hikari_module(hikari_application ALLOW hikari_core)
