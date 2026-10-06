# W1: the optional DirectShow playback adapter behind the general player port
# (legacy dshowplayer.cpp, DshowRenderer.cpp). Windows only.

# The DirectShow base classes legacy linked (Thirdparty/BaseClasses, built as
# Thirdparty/Build/BaseClasses/BaseClasses.vcxproj built them: Unicode, the
# dynamic CRT). Third-party code: its own language level, no warnings.
set(_hikari_baseclasses "${PROJECT_SOURCE_DIR}/Thirdparty/BaseClasses")
set(_hikari_baseclasses_sources "")
foreach(name amextra amfilter amvideo arithutil combase cprop ctlutil ddmm dllentry dllsetup mtype outputq perflog
             pstream pullpin refclock renbase schedule seekpt source strmctl sysclock transfrm transip videoctl
             vtrans winctrl winutil wxdebug wxlist wxutil)
    list(APPEND _hikari_baseclasses_sources "${_hikari_baseclasses}/${name}.cpp")
endforeach()
add_library(hikari_dshow_baseclasses STATIC ${_hikari_baseclasses_sources})
target_include_directories(hikari_dshow_baseclasses SYSTEM PUBLIC "${_hikari_baseclasses}")
target_compile_definitions(hikari_dshow_baseclasses PUBLIC UNICODE _UNICODE PRIVATE _LIB
    "$<$<CONFIG:Debug>:_DEBUG>")
set_target_properties(hikari_dshow_baseclasses PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
target_compile_options(hikari_dshow_baseclasses PRIVATE /W0 /permissive)
target_link_libraries(hikari_dshow_baseclasses PUBLIC strmiids winmm ole32 oleaut32 uuid)
hikari_module(hikari_dshow_baseclasses ALLOW)

add_library(hikari_directshow STATIC directshow_player.cpp directshow_renderer.cpp directshow_renderer.h
    directshow_frames.cpp directshow_frames.h include/hikari/backends/directshow_player.h)
target_include_directories(hikari_directshow PUBLIC include)
target_link_libraries(hikari_directshow PUBLIC hikari_application PRIVATE hikari_dshow_baseclasses)
hikari_module(hikari_directshow ALLOW hikari_application hikari_core hikari_dshow_baseclasses)

# The adapter alone in an executable: its deployment manifest is read from this
# (tests/backends: directshow-manifest), and it plays a file from the command
# line for diagnosis on a Windows machine.
add_executable(hikari_directshow_probe directshow_probe.cpp)
target_link_libraries(hikari_directshow_probe PRIVATE hikari_directshow)
hikari_module(hikari_directshow_probe ALLOW hikari_directshow)
