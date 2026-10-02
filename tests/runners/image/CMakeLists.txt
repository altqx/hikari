# Controls for image comparison against committed references.
foreach(variant IN ITEMS controls failing_control)
    hikari_add_qttest(hikari_image_${variant} SOURCES image_control.cpp
        LIBRARIES hikari_image_testing Hikari::media_deps Qt6::Qml Qt6::Quick)
    set(failing 0)
    if(variant STREQUAL "failing_control")
        set(failing 1)
    endif()
    target_compile_definitions(hikari_image_${variant} PRIVATE HIKARI_IMAGE_FAILING=${failing}
        HIKARI_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/tests/references"
        HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
        HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/${variant}")
endforeach()
get_property(all_tests DIRECTORY PROPERTY TESTS)
foreach(t IN LISTS all_tests)
    set_property(TEST ${t} APPEND PROPERTY LABELS "image;control")
    if(t MATCHES "failing_control")
        set_property(TEST ${t} PROPERTY WILL_FAIL TRUE)
    endif()
endforeach()
