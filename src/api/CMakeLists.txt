# The Hikari API: typed, versioned operations over the application. No Qt.
add_library(hikari_api STATIC schema.cpp api.cpp definitions.cpp document_operations.cpp
    application_operations.cpp write_operations.cpp events.cpp media_operations.cpp file_operations.cpp
    skill_operations.cpp automation_operations.cpp folder.cpp folder_skills.cpp project_operations.cpp reference.cpp reference_markdown.cpp)
target_include_directories(hikari_api PUBLIC include)
target_link_libraries(hikari_api PUBLIC hikari_json hikari_application hikari_core PRIVATE hikari_regex)
hikari_module(hikari_api ALLOW hikari_json hikari_application hikari_core hikari_regex)
