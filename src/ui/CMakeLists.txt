# Qt Quick UI: shell, controls and presenters. Observes application state and
# submits commands; never a second authoritative document store.
qt_add_library(hikari_ui STATIC line_table_model.cpp line_table_model.h line_grid.cpp line_grid.h line_grid_accessible.cpp line_grid_accessible.h
    shell_controller.cpp shell_controller.h line_editor_controller.cpp line_editor_controller.h
    automation_dialog_controller.cpp automation_dialog_controller.h)
target_include_directories(hikari_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_qml_module(hikari_ui
    URI Hikari.Ui
    VERSION 1.0
    QML_FILES Main.qml AutomationDialog.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick)
hikari_module(hikari_ui ALLOW hikari_application hikari_core "Qt6::*" "hikari_ui*")
