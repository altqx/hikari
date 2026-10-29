# Qt Quick UI: shell, controls and presenters. Observes application state and
# submits commands; never a second authoritative document store.
qt_add_library(hikari_ui STATIC line_table_model.cpp line_table_model.h line_grid.cpp line_grid.h line_grid_accessible.cpp line_grid_accessible.h
    shell_controller.cpp shell_controller.h line_editor_controller.cpp line_editor_controller.h
    automation_dialog_controller.cpp automation_dialog_controller.h script_dialog_memory.cpp script_dialog_memory.h video_presenter.cpp video_presenter.h
    automation_manager_controller.cpp automation_manager_controller.h template_editor_controller.cpp template_editor_controller.h
    automation_services_qt.cpp automation_services_qt.h video_controller.cpp video_controller.h
    automation_runs.cpp automation_runs.h log_controller.cpp log_controller.h karaoke_report.cpp karaoke_report.h tag_buttons_controller.cpp tag_buttons_controller.h
    line_measures.cpp line_measures.h grid_filter_controller.cpp grid_filter_controller.h
    colour_picker_controller.cpp colour_picker_controller.h docking.cpp docking.h
    workspace_layout.cpp workspace_layout.h controls_style.h shell_menus.h shift_times_controller.cpp shift_times_controller.h
    editor_highlighter.cpp editor_highlighter.h
    settings_store.cpp settings_store.h settings_files.cpp settings_files.h toml_file.cpp toml_file.h list_files.cpp list_files.h dialog_placement.cpp dialog_placement.h
    audio_controller.cpp audio_controller.h audio_display_item.cpp audio_display_item.h
    visual_tools_controller.cpp visual_tools_controller.h shape_editor.cpp shape_editor.h
    tag_list_controller.cpp tag_list_controller.h
    all_tags_editor.cpp all_tags_editor.h time_field_mask.cpp time_field_mask.h
    script_resolution_prompt.cpp script_resolution_prompt.h
    number_field_mouse.cpp number_field_mouse.h key_router.cpp key_router.h)
target_include_directories(hikari_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_qml_module(hikari_ui
    URI Hikari.Ui
    VERSION 1.0
    QML_FILES Main.qml ShellMenu.qml ShellMenuItem.qml MacroMenuEntries.qml AutomationDialog.qml AutomationManager.qml TemplateEditor.qml AutomationFilePicker.qml FontPicker.qml ScriptPropertiesDialog.qml SelectLinesDialog.qml StyleManager.qml FixMinorErrors.qml HistoryWindow.qml ColumnsWindow.qml TranslationShiftWindow.qml SpellCheckerPanel.qml SettingsDialog.qml SearchTool.qml DockTitleBar.qml DockTabBar.qml
        DocumentTabBar.qml SessionWindows.qml)
# TS1: the Shift times panel and the dock-form pieces it is built from.
qt_target_qml_sources(hikari_ui QML_FILES ShiftTimesPanel.qml FormSection.qml SegmentedButtons.qml PrimaryButton.qml)
# SH2: the Home page, while no Document is open.
qt_target_qml_sources(hikari_ui QML_FILES HomePage.qml)
# TP1: the Styles panel.
qt_target_qml_sources(hikari_ui QML_FILES StylesPanel.qml)
# E6: the Line editor's tag list.
qt_target_qml_sources(hikari_ui QML_FILES TagListPopup.qml EditorTagList.qml)
# LE5: the custom tag buttons, as the strip shows them, and their popover.
qt_target_qml_sources(hikari_ui QML_FILES TagButton.qml TagButtonEditor.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick PRIVATE Qt6::QuickControls2 KDAB::kddockwidgets)
# V1: the presenter converts player frames kept as Y'CbCr planes in a shader.
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS ShaderTools)
target_sources(hikari_ui PRIVATE ycbcr_node.cpp ycbcr_node.h)
qt_add_shaders(hikari_ui "hikari_ui_shaders"
    BATCHABLE
    PREFIX "/hikari/ui"
    FILES shaders/ycbcr.vert shaders/ycbcr.frag)
# D3: whether X11 composites (QPlatformNativeInterface, ui/docking.cpp).
target_link_libraries(hikari_ui PRIVATE Qt6::GuiPrivate)
# Some engine headers include their siblings relative to the kddockwidgets directory.
get_target_property(hikari_kddw_include KDAB::kddockwidgets INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(hikari_ui PRIVATE "${hikari_kddw_include}/kddockwidgets")
# CF1 (#488): toml++ for the settings files (settings_files.h, toml_file.h).
target_link_libraries(hikari_ui PUBLIC Hikari::tomlplusplus)
hikari_module(hikari_ui ALLOW hikari_application hikari_core "Qt6::*" "hikari_ui*" "KDAB::*" Hikari::tomlplusplus)
# O2: the "Hotkey mapping" window and its questions.
# S4: the automation shell's message boxes and script editor picker.
qt_target_qml_sources(hikari_ui QML_FILES AutomationNotices.qml)
# AD3 (#312): a macro run's notice and progress card, and a line of a run's log.
qt_target_qml_sources(hikari_ui QML_FILES AutomationProgress.qml RunLogLine.qml)
# Y4-playres-ask: the question a macro reading a missing script resolution asks.
qt_target_qml_sources(hikari_ui QML_FILES ScriptResolutionDialog.qml)
# R1: the tab menu's "Subtitle comparison".
qt_target_qml_sources(hikari_ui QML_FILES SubtitleComparisonMenu.qml DocumentTabMenu.qml)
# R2: the reference tray (navigation, linked matching, the preview menu).
qt_target_qml_sources(hikari_ui QML_FILES ReferenceTray.qml)
# The Grids' hover text for their abbreviated headings.
qt_target_qml_sources(hikari_ui QML_FILES GridHeaderToolTip.qml)
# The windows' closing rows (DialogButtonBox, in the platform's order).
qt_target_qml_sources(hikari_ui QML_FILES WindowFooter.qml FooterButton.qml)
# P9: the association question, the tab commands' questions and the legacy autosaves.
qt_target_qml_sources(hikari_ui QML_FILES AssociationOffer.qml TabCommands.qml)
# ADR 0019: the update notices and the installing notice.
qt_target_qml_sources(hikari_ui QML_FILES UpdateReadyBar.qml UpdateInstallingWindow.qml)
# Y8: the font collector's window.
qt_target_qml_sources(hikari_ui QML_FILES FontCollectorDialog.qml)
# KA7: the Karaoke check window.
qt_target_qml_sources(hikari_ui QML_FILES KaraokeCheckDialog.qml)
# KA3: the Kanji timer window.
qt_target_qml_sources(hikari_ui QML_FILES KanjiTimerDialog.qml)
# O3: the legacy settings importer's window.
qt_target_qml_sources(hikari_ui QML_FILES SettingsImportDialog.qml SettingsImportReview.qml)
# FR1: Getting started, the setup of the first start.
qt_target_qml_sources(hikari_ui QML_FILES GettingStartedDialog.qml)
# Y6: Font catalogs, the catalog window and "Add fonts from subtitles".
qt_target_qml_sources(hikari_ui QML_FILES FontCatalogBar.qml FontCatalogWindow.qml FontsFromSubtitlesDialog.qml)
# PU3: the font picker's chips and renderer note; the one catalog window and
# "Add fonts from subtitles" window for every opener.
set_source_files_properties(FontWindows.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_target_qml_sources(hikari_ui QML_FILES FilterChip.qml FontResolutionNote.qml FontWindows.qml)
# Y9: GRID_SUBS_FROM_MKV's question, track chooser and progress.
qt_target_qml_sources(hikari_ui QML_FILES MatroskaSubtitles.qml)
# E4: the Line editor's metadata fields, counters and \an choice; its masked time fields
qt_target_qml_sources(hikari_ui QML_FILES LineInspector.qml LineCounters.qml LineEditorStrip.qml AlignmentChoice.qml TimeField.qml
    NumberField.qml)
# T1: the visual tool rail and the overlay over the Video canvas; VT1: the inspector bar below it and its fields.
qt_target_qml_sources(hikari_ui QML_FILES VisualToolRail.qml VisualOverlay.qml VisualToolBar.qml VisualPointerReadout.qml VisualValueField.qml VisualTagPicker.qml VisualLastChange.qml)
# T2-T6: the families' options in the bar (Position, Move, Scale, the rotations,
# the clips' point modes and Invert clip, ...) and the tools' dialogs.
qt_target_qml_sources(hikari_ui QML_FILES VisualToolOption.qml VisualToolDialogs.qml)
# T5: the drawing's "Edit shapes" window (PU5: a drawing's preview, a
# refused value's error under its field).
qt_target_qml_sources(hikari_ui QML_FILES ShapesEditionDialog.qml ShapeDrawing.qml FieldError.qml)
# T6: the all-tags tool's "Edit tags" window.
qt_target_qml_sources(hikari_ui QML_FILES AllTagsEditionDialog.qml)
# V3: video sources, tracks and chapters: the recent lists, the stream and
# chapter menus, the indexing progress, the next-file question and the dummy
# video dialog.
qt_target_qml_sources(hikari_ui QML_FILES RecentFilesMenu.qml VideoStreamsMenu.qml VideoChaptersMenu.qml
    VideoIndexingProgress.qml VideoFileQuestion.qml DummyVideoDialog.qml)
# WS3: the layout switcher, its menu (also View > Layout) and the Save layout
# and Manage layouts dialogs.
qt_target_qml_sources(hikari_ui QML_FILES LayoutSwitcher.qml LayoutMenu.qml SaveLayoutDialog.qml
    ManageLayoutsDialog.qml)
# P10: the status bar (legacy HikariStatusBar's nine fields) and the help
# text the menus and tool buttons show in its first field.
target_sources(hikari_ui PRIVATE status_bar_controller.cpp status_bar_controller.h)
set_source_files_properties(StatusHelp.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_target_qml_sources(hikari_ui QML_FILES StatusBar.qml StatusHelp.qml)
# K1: the vector icon set (docs/ux/icons.md), drawn by tools/icons/draw_icons.py:
# the SVGs and their manifest as module resources, the Icon item, the
# icon-only button, the menu items, tab button and dialog title that draw the
# set through IconTheme's image provider. Kept in this block of their own (not the shared lists
# above) so the parallel cards' edits do not meet.
target_sources(hikari_ui PRIVATE icon_theme.cpp icon_theme.h)
target_link_libraries(hikari_ui PRIVATE Qt6::Svg)
file(GLOB hikari_icon_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/*.svg")
qt_target_qml_sources(hikari_ui QML_FILES Icon.qml IconButton.qml
    IconTabButton.qml IconDialogHeader.qml IconToolButton.qml IconTextButton.qml
    RESOURCES icons/manifest.json ${hikari_icon_files})
# K2: the theme layer (docs/ux/visual-language.md): the Theme singleton and
# the appearance state, the Options dialog's Appearance page and its focus
# ring, the focused panel's accent corner mark, and the docking engine's
# separators (D1's splitter), group frames and floating windows in the theme's
# colours.
target_sources(hikari_ui PRIVATE theme.cpp theme.h)
# K2 on Windows: the title bar of every framed window follows the theme
# (DWMWA_USE_IMMERSIVE_DARK_MODE, title_bars.h).
target_sources(hikari_ui PRIVATE title_bars.cpp title_bars.h)
if(WIN32)
    target_link_libraries(hikari_ui PRIVATE dwmapi)
endif()
qt_target_qml_sources(hikari_ui QML_FILES AppearancePage.qml SettingsSection.qml SettingsRow.qml FocusRing.qml PanelFocusMark.qml DockSeparator.qml DockGroup.qml DockFloatingWindow.qml)
# K2: the controls style (Fusion with the theme's control outlines), chosen by
# theme::chooseControlsStyle and imported as a static plugin by theme.cpp.
add_subdirectory(style)
target_link_libraries(hikari_ui PUBLIC hikari_ui_styleplugin)
# V4: the video view's zoom, aspect, volume, context menu and snapshots.
target_sources(hikari_ui PRIVATE video_view_controller.cpp video_view_controller.h)
# V5: video fullscreen and the monitor choice.
target_sources(hikari_ui PRIVATE video_fullscreen_controller.cpp video_fullscreen_controller.h)
qt_target_qml_sources(hikari_ui QML_FILES VideoFullscreen.qml)
qt_target_qml_sources(hikari_ui QML_FILES VideoContextMenu.qml AspectRatioDialog.qml VideoZoomFrame.qml
    VideoRenderCost.qml # KE1-render-cost-overlay
    VideoTemplateBadge.qml # KE2: the Template editor's preview on the video
    VideoVolumeSlider.qml)
# V6: the video toolbar's seek-after and play-after lists.
qt_target_qml_sources(hikari_ui QML_FILES VideoFollowChoices.qml)
# AV1: the audio panel's output device menu.
qt_target_qml_sources(hikari_ui QML_FILES AudioOutputMenu.qml)
# AU1: the rows of the audio panel's zoom and speaker popovers.
qt_target_qml_sources(hikari_ui QML_FILES AudioSliderRow.qml)
# KA2 (#277): the audio panel's tap bar, the latency calibration window and
# the key caps their hints show.
qt_target_qml_sources(hikari_ui QML_FILES AudioTapBar.qml TapCalibrationWindow.qml Keycap.qml)
# E5: the Line editor's Not confirmed and Moving tags buttons. TM1: the
# Original text panel's toolbar, the Translation preset's question and the
# turn-off confirmation (the Translator mode check box is gone).
qt_target_qml_sources(hikari_ui QML_FILES TranslationToggles.qml OriginalTextTools.qml RunStrip.qml TranslationModeQuestion.qml
    TranslationModeOffConfirm.qml)
# Y7: the colour picker's screen dropper (QScreen grabs on Windows and X11,
# the desktop's Screenshot portal over D-Bus on Wayland).
target_sources(hikari_ui PRIVATE screen_sampler.cpp screen_sampler.h)
qt_target_qml_sources(hikari_ui QML_FILES ScreenDropperView.qml)
# SY2: the one colour picker, a popover, and its swatch grids.
qt_target_qml_sources(hikari_ui QML_FILES ColourPopover.qml ColourSwatchGrid.qml)
# SY3: font lists drawing each family in its own face.
target_sources(hikari_ui PRIVATE font_faces.cpp font_faces.h)
qt_target_qml_sources(hikari_ui QML_FILES FontFamilyRow.qml)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS DBus)
    target_link_libraries(hikari_ui PRIVATE Qt6::DBus)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_SCREEN_PORTAL)
endif()
# K2: Follow system theme reads the XDG settings portal's colour scheme where
# Qt's platform theme reports none (portal_colour_scheme.h).
if(UNIX AND NOT APPLE)
    target_sources(hikari_ui PRIVATE portal_colour_scheme.cpp portal_colour_scheme.h)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_COLOUR_SCHEME_PORTAL)
endif()
# D3: the dock chrome after MuseScore 4 (docs/docking.md, "Chrome"): the
# panel header's "⋯" button, the drop highlight and the floating header's
# system-move area.
qt_target_qml_sources(hikari_ui QML_FILES DockMenuButton.qml DockDropHighlight.qml DockSystemMoveArea.qml)
# D3: floating panels ask the window system for no frame
# (window_decorations.cpp). On Wayland that is the xdg-decoration protocol,
# vendored in wayland/ with xdg-shell, whose xdg_toplevel interface it names
# (Qt keeps its own copy private to its xdg-shell plugin).
target_sources(hikari_ui PRIVATE window_decorations.cpp window_decorations.h)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS WaylandClient)
    find_package(WaylandScanner REQUIRED)
    qt6_generate_wayland_protocol_client_sources(hikari_ui PRIVATE_CODE
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/wayland/xdg-decoration-unstable-v1.xml")
    set(hikari_xdg_shell_code "${CMAKE_CURRENT_BINARY_DIR}/wayland-xdg-shell-protocol.c")
    add_custom_command(OUTPUT "${hikari_xdg_shell_code}"
        COMMAND Wayland::Scanner private-code < "${CMAKE_CURRENT_SOURCE_DIR}/wayland/xdg-shell.xml" > "${hikari_xdg_shell_code}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wayland/xdg-shell.xml" Wayland::Scanner)
    target_sources(hikari_ui PRIVATE "${hikari_xdg_shell_code}")
    target_link_libraries(hikari_ui PRIVATE Qt6::WaylandClient Wayland::Client)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_WAYLAND_DECORATIONS)
endif()
# DX1 (#367): User guide's pages for the shell and the DG1 windows' "?" button.
target_sources(hikari_ui PRIVATE user_guide.cpp user_guide.h)
# SH1: the main window's own title bar (window_frame.h, MainTitleBar.qml):
# the frameless window, the window menu (Qt's Wayland shell surface; on X11
# the xcb connection Qt holds, looked up at run time) and on Windows the
# native frame (window_frame_win.cpp).
target_sources(hikari_ui PRIVATE window_frame.cpp window_frame.h window_frame_native.h)
if(WIN32)
    target_sources(hikari_ui PRIVATE window_frame_win.cpp)
    target_link_libraries(hikari_ui PRIVATE dwmapi shell32)
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS WaylandClientPrivate)
    target_link_libraries(hikari_ui PRIVATE Qt6::WaylandClientPrivate ${CMAKE_DL_LIBS})
endif()
qt_target_qml_sources(hikari_ui QML_FILES MainTitleBar.qml MainWindowEdges.qml TitleBarMenus.qml TitleBarMenuItem.qml
    TitleBarMoveArea.qml TitleBarButton.qml)
# DG1: the dialog frame (DialogWindow.qml, dialog_placement.h) and About, Credits and the update offer on it.
qt_target_qml_sources(hikari_ui QML_FILES DialogWindow.qml DialogTitleBar.qml AboutDialog.qml CreditsDialog.qml
    UpdateAvailable.qml)
# SH3: What's new from the release notes, on the Home page and in Help > What's new.
qt_target_qml_sources(hikari_ui QML_FILES LinkButton.qml ReleaseNotesView.qml WhatsNewCard.qml WhatsNewDialog.qml)
# CR1 (#452): the crash and freeze report, and Report an issue.
qt_target_qml_sources(hikari_ui QML_FILES CrashReportWindow.qml ReportIssueWindow.qml)
# PK1: the popup kit on DG1's frame (docs/ux/visual-language.md "Popups"): the message box, the popover,
# toasts and the inline bar, and the clash review, Choose styles, hotkey capture, list chooser and date picker;
# NT1: the application's message box (MessageHost) and the Log panel.
qt_target_qml_sources(hikari_ui QML_FILES MessageBox.qml DangerButton.qml Popover.qml PopoverBackground.qml
    ToastStack.qml InlineBar.qml ClashReview.qml ChooseStyles.qml HotkeyCapture.qml ListChooser.qml DatePicker.qml
    MessageHost.qml LogPanel.qml)
# PU4: a question "Don't ask again" can hide.
qt_target_qml_sources(hikari_ui QML_FILES HidableQuestion.qml)
# PU4: Set new FPS on DG1's frame.
qt_target_qml_sources(hikari_ui QML_FILES SetNewFpsWindow.qml)
# PU1: the file and session popups on the kit: the lazily made message box, Unsaved changes, Autosaves and the
# startup offer.
qt_target_qml_sources(hikari_ui QML_FILES LazyMessageBox.qml UnsavedChanges.qml Autosaves.qml StartupOffer.qml)
# PU6 (#308): the Spellchecker panel's added words window, the conversion
# and Resample subtitles windows.
qt_target_qml_sources(hikari_ui QML_FILES SpellingAddedWords.qml ConversionWindow.qml ResampleWindow.qml)
# The title bar's application icon: the packaged one (qrc:/hikari/ui/app-icon.png).
set(hikari_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/linux/icons/hicolor/64x64/apps/hikarisub.png")
set_source_files_properties("${hikari_app_icon}" PROPERTIES QT_RESOURCE_ALIAS "app-icon.png")
qt_add_resources(hikari_ui "hikari_app_icon" PREFIX "/hikari/ui" FILES "${hikari_app_icon}")
# KA1 (#276): Karaoke's English hyphenation (LibreOffice's hyph_en_US.dic,
# BSD-style; README_hyph_en_US.txt beside it).
qt_add_resources(hikari_ui "hikari_hyphenation" PREFIX "/hikari/ui" FILES hyphenation/hyph_en_US.dic)
# KE3: the built-in karaoke effect presets the Template editor inserts.
file(GLOB _hikari_karaoke_presets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/application/karaoke-presets/*.ass")
qt_add_resources(hikari_ui "hikari_karaoke_presets" PREFIX "/hikari/ui" BASE "${PROJECT_SOURCE_DIR}/src/application"
    FILES ${_hikari_karaoke_presets})
