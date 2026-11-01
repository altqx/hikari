add_test(NAME modules.direction-violation-rejected
    COMMAND "${CMAKE_COMMAND}" -DCASE=violation -P "${CMAKE_CURRENT_SOURCE_DIR}/check_direction.cmake")
set_tests_properties(modules.direction-violation-rejected PROPERTIES WILL_FAIL TRUE LABELS "modules;control")
add_test(NAME modules.direction-allowed-accepted
    COMMAND "${CMAKE_COMMAND}" -DCASE=allowed -P "${CMAKE_CURRENT_SOURCE_DIR}/check_direction.cmake")
set_tests_properties(modules.direction-allowed-accepted PROPERTIES LABELS "modules;control")

add_test(NAME modules.desktop-handoffs
    COMMAND "${CMAKE_COMMAND}" -DROOT=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_desktop_handoffs.cmake")
set_tests_properties(modules.desktop-handoffs PROPERTIES LABELS "modules")
add_test(NAME modules.desktop-handoffs-violation-rejected
    COMMAND "${CMAKE_COMMAND}" -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/desktop-handoff-violation
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_desktop_handoffs.cmake")
set_tests_properties(modules.desktop-handoffs-violation-rejected PROPERTIES WILL_FAIL TRUE LABELS "modules;control")

set(qml-idioms_rejection "QML text built the pre-6.12 way:.*Violation.qml")
set(qml-debug_rejection "reaches CI, tests or packages:.*workflowPresets release uses release")
foreach(check qml-idioms qml-debug)
    string(REPLACE "-" "_" script "check_${check}")
    add_test(NAME modules.${check}
        COMMAND "${CMAKE_COMMAND}" -DROOT=${PROJECT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/${script}.cmake")
    set_tests_properties(modules.${check} PROPERTIES LABELS "modules")
    add_test(NAME modules.${check}-violation-rejected
        COMMAND "${CMAKE_COMMAND}" -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/${check}-violation
            -P "${CMAKE_CURRENT_SOURCE_DIR}/${script}.cmake")
    set_tests_properties(modules.${check}-violation-rejected PROPERTIES
        PASS_REGULAR_EXPRESSION "${${check}_rejection}" LABELS "modules;control")
endforeach()

add_test(NAME modules.qmllint
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target hikari_ui_qmllint)
set_tests_properties(modules.qmllint PROPERTIES LABELS "modules;qml" TIMEOUT 120 RUN_SERIAL TRUE)

add_executable(hikari_app_startup startup.cpp)
target_link_libraries(hikari_app_startup PRIVATE hikari_composition
    hikari_test_url_recorder hikari_test_desktop_routes)
_hikari_add_qt_modes(hikari_app_startup app $<TARGET_FILE:hikari_app_startup>)
foreach(mode offscreen xvfb)
    if(TEST hikari_app_startup.${mode})
        set_property(TEST hikari_app_startup.${mode} APPEND PROPERTY ENVIRONMENT
            "HIKARISUB_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/app-${mode}/user"
            "XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/app-${mode}/cache")
        if(WIN32)
            set_property(TEST hikari_app_startup.${mode} APPEND PROPERTY ENVIRONMENT
                "LOCALAPPDATA=${CMAKE_CURRENT_BINARY_DIR}/app-${mode}/local")
        endif()
    endif()
endforeach()

if(UNIX AND NOT APPLE)
    add_test(NAME modules.first-start-system-ui-languages
        COMMAND "${CMAKE_COMMAND}" -DSTARTUP=$<TARGET_FILE:hikari_app_startup>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/first-start -P "${CMAKE_CURRENT_SOURCE_DIR}/check_first_start_language.cmake")
    set_tests_properties(modules.first-start-system-ui-languages PROPERTIES LABELS "modules;offscreen" TIMEOUT 300)
    add_test(NAME modules.user-folder
        COMMAND "${CMAKE_COMMAND}" -DSTARTUP=$<TARGET_FILE:hikari_app_startup>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/user-folder -P "${CMAKE_CURRENT_SOURCE_DIR}/check_user_folder.cmake")
    set_tests_properties(modules.user-folder PROPERTIES LABELS "modules;offscreen" TIMEOUT 300)
endif()
