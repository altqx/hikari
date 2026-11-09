qt_add_library(hikari_ui_plugin_api STATIC)
qt_add_qml_module(hikari_ui_plugin_api
    URI Hikari.Plugin
    VERSION 1.0
    QML_FILES Icon.qml IconButton.qml
    DEPENDENCIES Hikari.Ui)
target_link_libraries(hikari_ui_plugin_api PRIVATE Qt6::Quick)
hikari_module(hikari_ui_plugin_api ALLOW "Qt6::*" "hikari_ui*")
