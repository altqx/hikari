add_library(hikari_json STATIC json.cpp)
target_include_directories(hikari_json PUBLIC include)
hikari_module(hikari_json)
