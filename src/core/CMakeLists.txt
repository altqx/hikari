# Plain C++ subtitle core: no Qt, window, device or service-lookup dependency.
add_library(hikari_core STATIC module.cpp checked.cpp time.cpp frame_timeline.cpp ass_load.cpp ass_save.cpp style.cpp srt.cpp line_formats.cpp subtitle_load.cpp text_projection.cpp tag_commands.cpp editor_font_colour.cpp colour_space.cpp clipboard_rows.cpp line_groups.cpp legacy_regex.cpp spelling.cpp matroska.cpp tag_list.cpp)
target_include_directories(hikari_core PUBLIC include)
target_compile_features(hikari_core PUBLIC cxx_std_23)
# R1-pcre2: wxRegEx's engine, privately (16-bit, as wxString on Windows).
target_link_libraries(hikari_core PRIVATE PCRE2::16BIT)
hikari_module(hikari_core ALLOW PCRE2::16BIT)
