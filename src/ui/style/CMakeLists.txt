# K2: the Qt Quick Controls style the theme layer drives (docs/qt/ux/visual-language.md,
# "Hikari theme layer"): Fusion, with every control outline in the theme's
# boundary colour (the palette's Mid, the `line` role; the high-contrast
# border pick) instead of Fusion's window colour darkened, which all but
# vanishes on the Dark and High contrast black panels. Geometry, fills and
# the focused outline (the accent) stay Fusion's; any other control is
# Fusion's own (theme::chooseControlsStyle sets Fusion as the fallback style:
# Qt Quick Controls does not read a fallback from a static style's qmldir).
qt_add_library(hikari_ui_style STATIC)
qt_add_qml_module(hikari_ui_style
    URI HikariStyle
    VERSION 1.0
    QML_FILES Button.qml ToolButton.qml TextField.qml TextArea.qml SpinBox.qml ComboBox.qml
        CheckBox.qml RadioButton.qml GroupBox.qml Frame.qml Menu.qml TabBar.qml TabButton.qml)
target_link_libraries(hikari_ui_style PRIVATE Qt6::Quick Qt6::QuickControls2)
hikari_module(hikari_ui_style ALLOW "Qt6::*" "hikari_ui_style*")
