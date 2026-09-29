# Controls proving the QtTest and Qt Quick Test runners report both outcomes.
hikari_add_qttest(hikari_qttest_controls SOURCES qttest_controls.cpp LIBRARIES Qt6::Gui)
hikari_add_qttest(hikari_qttest_failing_control SOURCES qttest_failing_control.cpp)
hikari_add_quicktest(hikari_quicktest_controls QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml-pass")
hikari_add_quicktest(hikari_quicktest_failing_control QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml-fail")

# The failing controls must fail in every mode; CTest reports them as passing
# only while failure reporting works.
get_property(all_tests DIRECTORY PROPERTY TESTS)
foreach(t IN LISTS all_tests)
    if(t MATCHES "failing_control")
        set_property(TEST ${t} PROPERTY WILL_FAIL TRUE)
        set_property(TEST ${t} APPEND PROPERTY LABELS control)
    else()
        set_property(TEST ${t} APPEND PROPERTY LABELS control)
    endif()
endforeach()
