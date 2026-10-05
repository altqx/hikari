# B3 evidence: dependency-direction enforcement, QML lint and a real startup.

# The pure check must reject core linking Qt, and accept an allowed graph.
add_test(NAME modules.direction-violation-rejected
    COMMAND "${CMAKE_COMMAND}" -DCASE=violation -P "${CMAKE_CURRENT_SOURCE_DIR}/check_direction.cmake")
set_tests_properties(modules.direction-violation-rejected PROPERTIES WILL_FAIL TRUE LABELS "modules;control")
add_test(NAME modules.direction-allowed-accepted
    COMMAND "${CMAKE_COMMAND}" -DCASE=allowed -P "${CMAKE_CURRENT_SOURCE_DIR}/check_direction.cmake")
set_tests_properties(modules.direction-allowed-accepted PROPERTIES LABELS "modules;control")

add_test(NAME modules.qmllint
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target hikari_ui_qmllint)
set_tests_properties(modules.qmllint PROPERTIES LABELS "modules;qml" TIMEOUT 120)

add_executable(hikari_app_startup startup.cpp)
target_link_libraries(hikari_app_startup PRIVATE hikari_composition)
_hikari_add_qt_modes(hikari_app_startup app $<TARGET_FILE:hikari_app_startup>)

# O5-first-start-ui-languages: the composition's first start reads the
# system's UI languages (QLocale::system().uiLanguages()). Linux: Qt reads
# them from the environment the script sets.
if(UNIX AND NOT APPLE)
    add_test(NAME modules.first-start-system-ui-languages
        COMMAND "${CMAKE_COMMAND}" -DSTARTUP=$<TARGET_FILE:hikari_app_startup>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/first-start -P "${CMAKE_CURRENT_SOURCE_DIR}/check_first_start_language.cmake")
    set_tests_properties(modules.first-start-system-ui-languages PROPERTIES LABELS "modules;offscreen" TIMEOUT 300)
endif()
