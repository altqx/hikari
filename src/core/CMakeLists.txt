# Plain C++ subtitle core: no Qt, window, device or service-lookup dependency.
add_library(hikari_core STATIC module.cpp)
target_include_directories(hikari_core PUBLIC include)
target_compile_features(hikari_core PUBLIC cxx_std_20)
hikari_module(hikari_core ALLOW)
