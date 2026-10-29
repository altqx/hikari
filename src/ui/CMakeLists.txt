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
    visual_tools_controller.cpp visual_tools_controller.h command_names.cpp command_names.h shape_editor.cpp shape_editor.h
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
qt_target_qml_sources(hikari_ui QML_FILES ShiftTimesPanel.qml FormSection.qml SegmentedButtons.qml PrimaryButton.qml)
qt_target_qml_sources(hikari_ui QML_FILES HomePage.qml)
qt_target_qml_sources(hikari_ui QML_FILES StylesPanel.qml)
qt_target_qml_sources(hikari_ui QML_FILES TagListPopup.qml EditorTagList.qml)
qt_target_qml_sources(hikari_ui QML_FILES TagButton.qml TagButtonEditor.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick PRIVATE Qt6::QuickControls2 KDAB::kddockwidgets)
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS ShaderTools)
target_sources(hikari_ui PRIVATE ycbcr_node.cpp ycbcr_node.h)
qt_add_shaders(hikari_ui "hikari_ui_shaders"
    BATCHABLE
    PREFIX "/hikari/ui"
    FILES shaders/ycbcr.vert shaders/ycbcr.frag)
target_link_libraries(hikari_ui PRIVATE Qt6::GuiPrivate)
# Some KDDW headers include their siblings relative to the kddockwidgets directory.
get_target_property(hikari_kddw_include KDAB::kddockwidgets INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(hikari_ui PRIVATE "${hikari_kddw_include}/kddockwidgets")
target_link_libraries(hikari_ui PUBLIC Hikari::tomlplusplus)
hikari_module(hikari_ui ALLOW hikari_application hikari_core "Qt6::*" "hikari_ui*" "KDAB::*" Hikari::tomlplusplus)
qt_target_qml_sources(hikari_ui QML_FILES AutomationNotices.qml)
qt_target_qml_sources(hikari_ui QML_FILES AutomationProgress.qml RunLogLine.qml)
qt_target_qml_sources(hikari_ui QML_FILES ScriptResolutionDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES SubtitleComparisonMenu.qml DocumentTabMenu.qml)
qt_target_qml_sources(hikari_ui QML_FILES ReferenceTray.qml)
qt_target_qml_sources(hikari_ui QML_FILES GridHeaderToolTip.qml)
qt_target_qml_sources(hikari_ui QML_FILES WindowFooter.qml FooterButton.qml)
qt_target_qml_sources(hikari_ui QML_FILES AssociationOffer.qml TabCommands.qml)
qt_target_qml_sources(hikari_ui QML_FILES UpdateReadyBar.qml UpdateInstallingWindow.qml)
qt_target_qml_sources(hikari_ui QML_FILES FontCollectorDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES KaraokeCheckDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES KanjiTimerDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES SettingsImportDialog.qml SettingsImportReview.qml)
qt_target_qml_sources(hikari_ui QML_FILES GettingStartedDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES FontCatalogBar.qml FontCatalogWindow.qml FontsFromSubtitlesDialog.qml)
set_source_files_properties(FontWindows.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_target_qml_sources(hikari_ui QML_FILES FilterChip.qml FontResolutionNote.qml FontWindows.qml)
qt_target_qml_sources(hikari_ui QML_FILES MatroskaSubtitles.qml)
qt_target_qml_sources(hikari_ui QML_FILES LineInspector.qml LineCounters.qml LineEditorStrip.qml AlignmentChoice.qml TimeField.qml
    NumberField.qml)
qt_target_qml_sources(hikari_ui QML_FILES VisualToolRail.qml VisualOverlay.qml VisualToolBar.qml VisualPointerReadout.qml VisualValueField.qml VisualTagPicker.qml VisualLastChange.qml)
qt_target_qml_sources(hikari_ui QML_FILES VisualToolOption.qml VisualToolDialogs.qml)
qt_target_qml_sources(hikari_ui QML_FILES ShapesEditionDialog.qml ShapeDrawing.qml FieldError.qml)
qt_target_qml_sources(hikari_ui QML_FILES AllTagsEditionDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES RecentFilesMenu.qml VideoStreamsMenu.qml VideoChaptersMenu.qml
    VideoIndexingProgress.qml VideoFileQuestion.qml DummyVideoDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES LayoutSwitcher.qml LayoutMenu.qml SaveLayoutDialog.qml
    ManageLayoutsDialog.qml)
target_sources(hikari_ui PRIVATE status_bar_controller.cpp status_bar_controller.h)
set_source_files_properties(StatusHelp.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_target_qml_sources(hikari_ui QML_FILES StatusBar.qml StatusHelp.qml)
target_sources(hikari_ui PRIVATE icon_theme.cpp icon_theme.h)
target_link_libraries(hikari_ui PRIVATE Qt6::Svg)
file(GLOB hikari_icon_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/*.svg")
qt_target_qml_sources(hikari_ui QML_FILES Icon.qml IconButton.qml
    IconTabButton.qml IconDialogHeader.qml IconToolButton.qml IconTextButton.qml
    RESOURCES icons/manifest.json ${hikari_icon_files})
target_sources(hikari_ui PRIVATE theme.cpp theme.h)
target_sources(hikari_ui PRIVATE title_bars.cpp title_bars.h)
if(WIN32)
    target_link_libraries(hikari_ui PRIVATE dwmapi)
endif()
qt_target_qml_sources(hikari_ui QML_FILES AppearancePage.qml SettingsSection.qml SettingsRow.qml FocusRing.qml PanelFocusMark.qml DockSeparator.qml DockGroup.qml DockFloatingWindow.qml)
add_subdirectory(style)
target_link_libraries(hikari_ui PUBLIC hikari_ui_styleplugin)
target_sources(hikari_ui PRIVATE video_view_controller.cpp video_view_controller.h)
target_sources(hikari_ui PRIVATE video_fullscreen_controller.cpp video_fullscreen_controller.h)
qt_target_qml_sources(hikari_ui QML_FILES VideoFullscreen.qml)
qt_target_qml_sources(hikari_ui QML_FILES VideoContextMenu.qml AspectRatioDialog.qml VideoZoomFrame.qml
    VideoRenderCost.qml
    VideoTemplateBadge.qml
    VideoVolumeSlider.qml)
qt_target_qml_sources(hikari_ui QML_FILES VideoFollowChoices.qml)
qt_target_qml_sources(hikari_ui QML_FILES AudioOutputMenu.qml)
qt_target_qml_sources(hikari_ui QML_FILES AudioSliderRow.qml)
qt_target_qml_sources(hikari_ui QML_FILES AudioTapBar.qml TapCalibrationWindow.qml Keycap.qml)
qt_target_qml_sources(hikari_ui QML_FILES TranslationToggles.qml OriginalTextTools.qml RunStrip.qml TranslationModeQuestion.qml
    TranslationModeOffConfirm.qml)
target_sources(hikari_ui PRIVATE screen_sampler.cpp screen_sampler.h)
qt_target_qml_sources(hikari_ui QML_FILES ScreenDropperView.qml)
qt_target_qml_sources(hikari_ui QML_FILES ColourPopover.qml ColourSwatchGrid.qml)
target_sources(hikari_ui PRIVATE font_faces.cpp font_faces.h)
qt_target_qml_sources(hikari_ui QML_FILES FontFamilyRow.qml)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS DBus)
    target_link_libraries(hikari_ui PRIVATE Qt6::DBus)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_SCREEN_PORTAL)
endif()
if(UNIX AND NOT APPLE)
    target_sources(hikari_ui PRIVATE portal_colour_scheme.cpp portal_colour_scheme.h)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_COLOUR_SCHEME_PORTAL)
endif()
qt_target_qml_sources(hikari_ui QML_FILES DockMenuButton.qml DockDropHighlight.qml DockSystemMoveArea.qml)
# xdg-decoration names xdg-shell's xdg_toplevel, and Qt keeps its own xdg-shell code private, so both are vendored.
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
target_sources(hikari_ui PRIVATE user_guide.cpp user_guide.h)
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
qt_target_qml_sources(hikari_ui QML_FILES DialogWindow.qml DialogTitleBar.qml AboutDialog.qml CreditsDialog.qml
    UpdateAvailable.qml)
qt_target_qml_sources(hikari_ui QML_FILES LinkButton.qml ReleaseNotesView.qml WhatsNewCard.qml WhatsNewDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES CrashReportWindow.qml ReportIssueWindow.qml)
qt_target_qml_sources(hikari_ui QML_FILES MessageBox.qml DangerButton.qml Popover.qml PopoverBackground.qml
    ToastStack.qml InlineBar.qml ClashReview.qml ChooseStyles.qml HotkeyCapture.qml ListChooser.qml DatePicker.qml
    MessageHost.qml LogPanel.qml)
qt_target_qml_sources(hikari_ui QML_FILES HidableQuestion.qml)
qt_target_qml_sources(hikari_ui QML_FILES SetNewFpsWindow.qml)
qt_target_qml_sources(hikari_ui QML_FILES LazyMessageBox.qml UnsavedChanges.qml Autosaves.qml StartupOffer.qml)
qt_target_qml_sources(hikari_ui QML_FILES SpellingAddedWords.qml ConversionWindow.qml ResampleWindow.qml)
set(hikari_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/linux/icons/hicolor/64x64/apps/hikarisub.png")
set_source_files_properties("${hikari_app_icon}" PROPERTIES QT_RESOURCE_ALIAS "app-icon.png")
qt_add_resources(hikari_ui "hikari_app_icon" PREFIX "/hikari/ui" FILES "${hikari_app_icon}")
# LibreOffice's hyph_en_US.dic (BSD-style; README_hyph_en_US.txt beside it).
qt_add_resources(hikari_ui "hikari_hyphenation" PREFIX "/hikari/ui" FILES hyphenation/hyph_en_US.dic)
file(GLOB _hikari_karaoke_presets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/application/karaoke-presets/*.ass")
qt_add_resources(hikari_ui "hikari_karaoke_presets" PREFIX "/hikari/ui" BASE "${PROJECT_SOURCE_DIR}/src/application"
    FILES ${_hikari_karaoke_presets})
