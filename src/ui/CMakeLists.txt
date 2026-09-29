# Qt Quick UI: shell, controls and presenters. Observes application state and
# submits commands; never a second authoritative document store.
qt_add_library(hikari_ui STATIC)
qt_add_qml_module(hikari_ui
    URI Hikari.Ui
    VERSION 1.0
    QML_FILES Main.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick)
hikari_module(hikari_ui ALLOW hikari_application hikari_core "Qt6::*" "hikari_ui*")
