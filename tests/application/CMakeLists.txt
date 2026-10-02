hikari_add_gtest(hikari_application_write_tests SOURCES write_coordinator_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_edit_tests SOURCES edit_session_tests.cpp
    LIBRARIES hikari_application LABELS application)
