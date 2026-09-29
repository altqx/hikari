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
