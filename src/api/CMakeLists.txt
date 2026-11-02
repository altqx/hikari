# The Hikari API: typed, versioned operations over the application. No Qt.
add_library(hikari_api STATIC json.cpp schema.cpp api.cpp definitions.cpp document_operations.cpp
    application_operations.cpp write_operations.cpp events.cpp)
target_include_directories(hikari_api PUBLIC include)
target_link_libraries(hikari_api PUBLIC hikari_application hikari_core)
hikari_module(hikari_api ALLOW hikari_application hikari_core)
