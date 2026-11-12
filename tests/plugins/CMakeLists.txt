hikari_add_gtest(hikari_plugins_tests SOURCES manifest_tests.cpp marketplace_tests.cpp package_tests.cpp installer_tests.cpp
    project_plugins_tests.cpp surface_tests.cpp test_signer.cpp
    LIBRARIES hikari_plugins Hikari::sodium LABELS plugins)
hikari_add_gtest(hikari_plugin_state_tests SOURCES plugin_state_tests.cpp
    LIBRARIES hikari_plugin_state Hikari::luajit LABELS plugins)
