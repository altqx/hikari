# Qt Quick UI: shell, controls and presenters. Observes application state and
# submits commands; never a second authoritative document store.
qt_add_library(hikari_ui STATIC line_table_model.cpp line_table_model.h line_grid.cpp line_grid.h line_grid_accessible.cpp line_grid_accessible.h
    shell_controller.cpp shell_controller.h line_editor_controller.cpp line_editor_controller.h
    automation_dialog_controller.cpp automation_dialog_controller.h video_presenter.cpp video_presenter.h
    automation_manager_controller.cpp automation_manager_controller.h
    automation_services_qt.cpp automation_services_qt.h video_controller.cpp video_controller.h
    log_controller.cpp log_controller.h tag_buttons_controller.cpp tag_buttons_controller.h
    line_measures.cpp line_measures.h grid_filter_controller.cpp grid_filter_controller.h
    colour_picker_controller.cpp colour_picker_controller.h docking.cpp docking.h
    workspace_layout.cpp workspace_layout.h shift_times_controller.cpp shift_times_controller.h
    spelling_text.cpp spelling_text.h spelling_highlighter.cpp spelling_highlighter.h)
target_include_directories(hikari_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_qml_module(hikari_ui
    URI Hikari.Ui
    VERSION 1.0
    QML_FILES Main.qml AutomationDialog.qml AutomationManager.qml AutomationFilePicker.qml FontDialog.qml ColourPickerDialog.qml ScriptPropertiesDialog.qml SelectLinesDialog.qml StyleManager.qml SpellCheckerDialog.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick PRIVATE KDAB::kddockwidgets)
# Some engine headers include their siblings relative to the kddockwidgets directory.
get_target_property(hikari_kddw_include KDAB::kddockwidgets INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(hikari_ui PRIVATE "${hikari_kddw_include}/kddockwidgets")
hikari_module(hikari_ui ALLOW hikari_application hikari_core "Qt6::*" "hikari_ui*" "KDAB::*")
