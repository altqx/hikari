# B3 evidence: dependency-direction enforcement, QML lint and a real startup.

# The pure check must reject core linking Qt, and accept an allowed graph.
add_test(NAME modules.direction-violation-rejected
    COMMAND "${CMAKE_COMMAND}" -DCASE=violation -P "${CMAKE_CURRENT_SOURCE_DIR}/check_direction.cmake")
set_tests_properties(modules.direction-violation-rejected PROPERTIES WILL_FAIL TRUE LABELS "modules;control")
add_test(NAME modules.direction-allowed-accepted
    COMMAND "${CMAKE_COMMAND}" -DCASE=allowed -P "${CMAKE_CURRENT_SOURCE_DIR}/check_direction.cmake")
set_tests_properties(modules.direction-allowed-accepted PROPERTIES LABELS "modules;control")

# 38e: every hand-off to the desktop goes through backends::desktop, whose
# routes the tests replace; the control must be rejected.
add_test(NAME modules.desktop-handoffs
    COMMAND "${CMAKE_COMMAND}" -DROOT=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_desktop_handoffs.cmake")
set_tests_properties(modules.desktop-handoffs PROPERTIES LABELS "modules")
add_test(NAME modules.desktop-handoffs-violation-rejected
    COMMAND "${CMAKE_COMMAND}" -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/desktop-handoff-violation
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_desktop_handoffs.cmake")
set_tests_properties(modules.desktop-handoffs-violation-rejected PROPERTIES WILL_FAIL TRUE LABELS "modules;control")

add_test(NAME modules.qmllint
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target hikari_ui_qmllint)
set_tests_properties(modules.qmllint PROPERTIES LABELS "modules;qml" TIMEOUT 120)

add_executable(hikari_app_startup startup.cpp)
target_link_libraries(hikari_app_startup PRIVATE hikari_composition
    hikari_test_url_recorder hikari_test_desktop_routes) # 38e: the desktop recorded
_hikari_add_qt_modes(hikari_app_startup app $<TARGET_FILE:hikari_app_startup>)
# UD1: the starts keep to a user folder of their own, so they never move or
# seed the user's real one.
foreach(mode offscreen xvfb)
    if(TEST app.${mode})
        set_property(TEST app.${mode} APPEND PROPERTY ENVIRONMENT
            "HIKARISUB_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/app-${mode}/user"
            "XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/app-${mode}/cache")
    endif()
endforeach()

# O5-first-start-ui-languages: the composition's first start reads the
# system's UI languages (QLocale::system().uiLanguages()). Linux: Qt reads
# them from the environment the script sets.
if(UNIX AND NOT APPLE)
    add_test(NAME modules.first-start-system-ui-languages
        COMMAND "${CMAKE_COMMAND}" -DSTARTUP=$<TARGET_FILE:hikari_app_startup>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/first-start -P "${CMAKE_CURRENT_SOURCE_DIR}/check_first_start_language.cmake")
    set_tests_properties(modules.first-start-system-ui-languages PROPERTIES LABELS "modules;offscreen" TIMEOUT 300)
    # UD1 (#362): the real composition's user folder under XDG_CONFIG_HOME
    # and HIKARISUB_USER_DIR, the bundled files seeded from the build tree's
    # manifest, and the one-time move from a populated and an empty old folder.
    add_test(NAME modules.user-folder
        COMMAND "${CMAKE_COMMAND}" -DSTARTUP=$<TARGET_FILE:hikari_app_startup>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/user-folder -P "${CMAKE_CURRENT_SOURCE_DIR}/check_user_folder.cmake")
    set_tests_properties(modules.user-folder PROPERTIES LABELS "modules;offscreen" TIMEOUT 300)
endif()
