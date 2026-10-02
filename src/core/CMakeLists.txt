# Plain C++ subtitle core: no Qt, window, device or service-lookup dependency.
add_library(hikari_core STATIC module.cpp checked.cpp time.cpp frame_timeline.cpp ass_load.cpp ass_save.cpp style.cpp srt.cpp line_formats.cpp subtitle_load.cpp text_projection.cpp)
target_include_directories(hikari_core PUBLIC include)
target_compile_features(hikari_core PUBLIC cxx_std_23)
hikari_module(hikari_core ALLOW)
