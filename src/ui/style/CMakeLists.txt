# Qt Quick Controls ignores a static style's qmldir fallback; theme::chooseControlsStyle sets Fusion.
qt_add_library(hikari_ui_style STATIC)
qt_add_qml_module(hikari_ui_style
    URI HikariStyle
    VERSION 1.0
    QML_FILES Button.qml ToolButton.qml TextField.qml TextArea.qml SpinBox.qml ComboBox.qml
        CheckBox.qml RadioButton.qml GroupBox.qml Frame.qml Menu.qml TabBar.qml TabButton.qml Slider.qml
        FocusFrame.qml
    SOURCES style_colours.cpp style_colours.h)
target_link_libraries(hikari_ui_style PRIVATE Qt6::Quick Qt6::QuickControls2)
hikari_module(hikari_ui_style ALLOW "Qt6::*" "hikari_ui_style*")
