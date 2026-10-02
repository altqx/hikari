if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    hikari_add_gtest(hikari_backends_posix_file_tests SOURCES posix_file_port_tests.cpp
        LIBRARIES hikari_backends LABELS backends)
    hikari_add_gtest(hikari_backends_open_save_tests SOURCES open_save_reopen_tests.cpp
        LIBRARIES hikari_backends LABELS backends)
    target_compile_definitions(hikari_backends_open_save_tests PRIVATE
        HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")
endif()
