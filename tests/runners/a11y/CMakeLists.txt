# Controls for accessibility-tree checks: a scene whose roles, names and Tab
# order are asserted, and a failing twin asserting a name that does not exist.
foreach(variant IN ITEMS controls failing_control)
    hikari_add_qttest(hikari_a11y_${variant} SOURCES a11y_control.cpp
        LIBRARIES hikari_a11y_testing Qt6::Qml Qt6::Quick)
    set(expect_missing 0)
    if(variant STREQUAL "failing_control")
        set(expect_missing 1)
    endif()
    target_compile_definitions(hikari_a11y_${variant} PRIVATE HIKARI_A11Y_EXPECT_MISSING=${expect_missing})
endforeach()
get_property(all_tests DIRECTORY PROPERTY TESTS)
foreach(t IN LISTS all_tests)
    set_property(TEST ${t} APPEND PROPERTY LABELS "a11y;control")
    if(t MATCHES "failing_control")
        set_property(TEST ${t} PROPERTY WILL_FAIL TRUE)
    endif()
endforeach()
