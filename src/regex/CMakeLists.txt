add_library(hikari_regex STATIC regex.cpp)
target_include_directories(hikari_regex PUBLIC include)
target_link_libraries(hikari_regex PRIVATE PCRE2::8BIT)
hikari_module(hikari_regex ALLOW PCRE2::8BIT)
