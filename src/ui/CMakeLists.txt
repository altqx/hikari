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
    workspace_layout.cpp workspace_layout.h controls_style.h shift_times_controller.cpp shift_times_controller.h
    spelling_text.cpp spelling_text.h spelling_highlighter.cpp spelling_highlighter.h
    settings_store.cpp settings_store.h
    audio_controller.cpp audio_controller.h audio_display_item.cpp audio_display_item.h
    visual_tools_controller.cpp visual_tools_controller.h
    tag_list_controller.cpp tag_list_controller.h)
target_include_directories(hikari_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_qml_module(hikari_ui
    URI Hikari.Ui
    VERSION 1.0
    QML_FILES Main.qml ShellMenu.qml ShellMenuItem.qml AutomationDialog.qml AutomationManager.qml AutomationFilePicker.qml FontDialog.qml ColourPickerDialog.qml ScriptPropertiesDialog.qml SelectLinesDialog.qml StyleManager.qml MisspellReplacerDialog.qml SpellCheckerDialog.qml SettingsDialog.qml SearchTool.qml DockTitleBar.qml DockTabBar.qml
        DocumentTabBar.qml SessionWindows.qml)
# E6: the Line editor's tag list.
qt_target_qml_sources(hikari_ui QML_FILES TagListPopup.qml EditorTagList.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick PRIVATE Qt6::QuickControls2 KDAB::kddockwidgets)
# Some engine headers include their siblings relative to the kddockwidgets directory.
get_target_property(hikari_kddw_include KDAB::kddockwidgets INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(hikari_ui PRIVATE "${hikari_kddw_include}/kddockwidgets")
hikari_module(hikari_ui ALLOW hikari_application hikari_core "Qt6::*" "hikari_ui*" "KDAB::*")
# O2: the "Hotkey mapping" window and its questions.
qt_target_qml_sources(hikari_ui QML_FILES HotkeyMapping.qml)
# S4: the automation shell's message boxes and script editor picker.
qt_target_qml_sources(hikari_ui QML_FILES AutomationNotices.qml)
# R1: the tab menu's "Subtitle comparison".
qt_target_qml_sources(hikari_ui QML_FILES SubtitleComparisonMenu.qml DocumentTabMenu.qml)
# R2: the reference tray (navigation, linked matching, the preview menu).
qt_target_qml_sources(hikari_ui QML_FILES ReferenceTray.qml)
# P9: the association question, the tab commands' questions and the legacy autosaves.
qt_target_qml_sources(hikari_ui QML_FILES AssociationOffer.qml TabCommands.qml LegacyAutosaveList.qml)
# Y8: the font collector's window.
qt_target_qml_sources(hikari_ui QML_FILES FontCollectorDialog.qml)
# O3: the legacy settings importer's window.
qt_target_qml_sources(hikari_ui QML_FILES SettingsImportDialog.qml)
# Y6: Font catalogs, the catalog window and "Add fonts from subtitles".
qt_target_qml_sources(hikari_ui QML_FILES FontCatalogBar.qml FontCatalogWindow.qml FontsFromSubtitlesDialog.qml)
# Y9: GRID_SUBS_FROM_MKV's question, track chooser and progress.
qt_target_qml_sources(hikari_ui QML_FILES MatroskaSubtitles.qml)
# E4: the Line editor's metadata fields, counters and \an choice
qt_target_qml_sources(hikari_ui QML_FILES LineInspector.qml LineCounters.qml AlignmentChoice.qml)
# T1: the visual tool rail, the overlay over the Video canvas and the values below it.
qt_target_qml_sources(hikari_ui QML_FILES VisualToolRail.qml VisualOverlay.qml VisualToolValues.qml)
# T2-T4: the families' options row (Position, Move, Scale, the rotations, the
# clips' point modes and Invert clip).
qt_target_qml_sources(hikari_ui QML_FILES VisualToolOptions.qml)
# V3: video sources, tracks and chapters: the recent lists, the stream and
# chapter menus, the indexing progress, the next-file question and the dummy
# video dialog.
qt_target_qml_sources(hikari_ui QML_FILES RecentFilesMenu.qml VideoStreamsMenu.qml VideoChaptersMenu.qml
    VideoIndexingProgress.qml VideoFileQuestion.qml DummyVideoDialog.qml)
# K1: the vector icon set (docs/qt/ux/icons.md), drawn by tools/icons/draw_icons.py:
# the SVGs and their manifest as module resources, the Icon item, the
# icon-only button, the menu items, tab button and dialog title that draw the
# set through IconTheme's image provider. Kept in this block of their own (not the shared lists
# above) so the parallel cards' edits do not meet.
target_sources(hikari_ui PRIVATE icon_theme.cpp icon_theme.h)
target_link_libraries(hikari_ui PRIVATE Qt6::Svg)
file(GLOB hikari_icon_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/*.svg")
qt_target_qml_sources(hikari_ui QML_FILES Icon.qml IconButton.qml
    IconTabButton.qml IconDialogHeader.qml IconToolButton.qml
    RESOURCES icons/manifest.json ${hikari_icon_files})
# K2: the theme layer (docs/qt/ux/visual-language.md): the Theme singleton and
# the appearance state, the Options dialog's Appearance page and its focus
# ring, and the docking engine's separators (D1's splitter), group frames and
# floating windows in the theme's colours.
target_sources(hikari_ui PRIVATE theme.cpp theme.h)
qt_target_qml_sources(hikari_ui QML_FILES AppearancePage.qml FocusRing.qml DockSeparator.qml DockGroup.qml DockFloatingWindow.qml)
# K2: the controls style (Fusion with the theme's control outlines), chosen by
# theme::chooseControlsStyle and imported as a static plugin by theme.cpp.
add_subdirectory(style)
target_link_libraries(hikari_ui PUBLIC hikari_ui_styleplugin)
# V4: the video view's zoom, aspect, volume, context menu and snapshots.
target_sources(hikari_ui PRIVATE video_view_controller.cpp video_view_controller.h)
qt_target_qml_sources(hikari_ui QML_FILES VideoContextMenu.qml AspectRatioDialog.qml VideoZoomFrame.qml
    VideoVolumeSlider.qml)
# V6: the video toolbar's seek-after and play-after lists.
qt_target_qml_sources(hikari_ui QML_FILES VideoFollowChoices.qml)
# E5: the Line editor's Translator mode check box and its Not confirmed and
# Moving tags buttons.
qt_target_qml_sources(hikari_ui QML_FILES TranslatorModeCheck.qml TranslationToggles.qml)
# Y7: the colour picker's screen dropper (QScreen grabs on Windows and X11,
# the desktop's Screenshot portal over D-Bus on Wayland) and the simple
# "Color picker" window.
target_sources(hikari_ui PRIVATE screen_sampler.cpp screen_sampler.h)
qt_target_qml_sources(hikari_ui QML_FILES ScreenDropperView.qml SimpleColourPicker.qml)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS DBus)
    target_link_libraries(hikari_ui PRIVATE Qt6::DBus)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_SCREEN_PORTAL)
endif()
