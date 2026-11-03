add_library(hikari_minisign STATIC minisign.cpp)
target_include_directories(hikari_minisign PUBLIC include)
target_link_libraries(hikari_minisign PRIVATE Hikari::sodium)
hikari_module(hikari_minisign ALLOW Hikari::sodium)
