# Controls proving Spix UI automation drives real Qt Quick items and reports
# both outcomes. The failing control expects the wrong result on purpose.
hikari_add_spix_test(hikari_ui_controls SOURCES ui_control.cpp
    DEFINITIONS HIKARI_UI_EXPECTED="clicked")
hikari_add_spix_test(hikari_ui_failing_control SOURCES ui_control.cpp
    DEFINITIONS HIKARI_UI_EXPECTED="never-set")

get_property(all_tests DIRECTORY PROPERTY TESTS)
foreach(t IN LISTS all_tests)
    set_property(TEST ${t} APPEND PROPERTY LABELS control)
    if(t MATCHES "failing_control")
        set_property(TEST ${t} PROPERTY WILL_FAIL TRUE)
    endif()
endforeach()
