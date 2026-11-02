hikari_add_gtest(hikari_plugins_tests SOURCES manifest_tests.cpp package_tests.cpp installer_tests.cpp test_signer.cpp
    LIBRARIES hikari_plugins Hikari::sodium LABELS plugins)
