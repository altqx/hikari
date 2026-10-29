# Object libraries' objects do not pass on transitively: link both directly.
add_library(hikari_test_url_recorder OBJECT url_recorder.cpp desktop_guard.h)
set_target_properties(hikari_test_url_recorder PROPERTIES AUTOMOC ON)
target_include_directories(hikari_test_url_recorder PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hikari_test_url_recorder PUBLIC Qt6::Core Qt6::Gui)

add_library(hikari_test_desktop_routes OBJECT desktop_routes.cpp)
target_link_libraries(hikari_test_desktop_routes PUBLIC hikari_test_url_recorder hikari_backends)
