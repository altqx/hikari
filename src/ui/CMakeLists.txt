qt_add_library(hikari_ui STATIC line_table_model.cpp line_table_model.h line_grid.cpp line_grid.h line_grid_accessible.cpp line_grid_accessible.h
    shell_controller.cpp shell_controller.h line_editor_controller.cpp line_editor_controller.h
    automation_dialog_controller.cpp automation_dialog_controller.h script_dialog_memory.cpp script_dialog_memory.h video_presenter.cpp video_presenter.h
    automation_manager_controller.cpp automation_manager_controller.h template_editor_controller.cpp template_editor_controller.h node_effects_controller.cpp node_effects_controller.h node_catalogue_text.cpp node_catalogue_text.h
    automation_services_qt.cpp automation_services_qt.h video_controller.cpp video_controller.h
    automation_runs.cpp automation_runs.h log_controller.cpp log_controller.h karaoke_report.cpp karaoke_report.h tag_buttons_controller.cpp tag_buttons_controller.h
    line_measures.cpp line_measures.h grid_filter_controller.cpp grid_filter_controller.h
    colour_picker_controller.cpp colour_picker_controller.h docking.cpp docking.h panel_dock.cpp panel_dock.h
    workspace_layout.cpp workspace_layout.h controls_style.h shell_menus.h shift_times_controller.cpp shift_times_controller.h presets_file.cpp presets_file.h tiered_presets.cpp tiered_presets.h
    editor_highlighter.cpp editor_highlighter.h
    settings_store.cpp settings_store.h settings_files.cpp settings_files.h project_file.cpp project_file.h project_credentials.cpp project_credentials.h toml_file.cpp toml_file.h list_files.cpp list_files.h dialog_placement.cpp dialog_placement.h
    audio_controller.cpp audio_controller.h audio_display_item.cpp audio_display_item.h
    visual_tools_controller.cpp visual_tools_controller.h command_names.cpp command_names.h shape_editor.cpp shape_editor.h
    tag_list_controller.cpp tag_list_controller.h
    all_tags_editor.cpp all_tags_editor.h time_field_mask.cpp time_field_mask.h
    script_resolution_prompt.cpp script_resolution_prompt.h
    number_field_mouse.cpp number_field_mouse.h key_router.cpp key_router.h wheel_scroll.cpp wheel_scroll.h)
target_include_directories(hikari_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_qml_module(hikari_ui
    URI Hikari.Ui
    VERSION 1.0
    QML_FILES Main.qml ShellMenu.qml ShellMenuItem.qml MacroMenuEntries.qml AutomationDialog.qml AutomationManager.qml TemplateEditor.qml AutomationFilePicker.qml FontPicker.qml ScriptPropertiesDialog.qml SelectLinesDialog.qml StyleManager.qml HistoryWindow.qml ColumnsWindow.qml TranslationShiftWindow.qml SpellCheckerPanel.qml SettingsDialog.qml SearchTool.qml DockTitleBar.qml DockTabBar.qml
        DocumentTabBar.qml SessionWindows.qml)
list(APPEND hikari_ui_qml_files ShiftTimesPanel.qml TimingPostprocessorPanel.qml TimingAssistantView.qml ShiftPresetBar.qml ShiftPreview.qml
    ShiftLines.qml ShiftNeed.qml FormSection.qml SegmentedButtons.qml PrimaryButton.qml)
list(APPEND hikari_ui_qml_files HomePage.qml RecentList.qml)
list(APPEND hikari_ui_qml_files StylesPanel.qml)
list(APPEND hikari_ui_qml_files ChangeAlignmentMenu.qml AlignDistributeMenu.qml PhantomMenu.qml CycleValuesPage.qml)
list(APPEND hikari_ui_qml_files TagListPopup.qml EditorTagList.qml)
list(APPEND hikari_ui_qml_files TagButton.qml TagButtonEditor.qml)
list(APPEND hikari_ui_qml_files NodeGraphCanvas.qml NodePort.qml NodeAddPopup.qml NodeInspector.qml
    NodeSourceLines.qml NodeModeBar.qml NodePresetMenu.qml NodeLibraryMenu.qml)
target_link_libraries(hikari_ui PUBLIC hikari_application Qt6::Quick PRIVATE Qt6::QuickControls2 KDAB::kddockwidgets)
find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS ShaderTools)
target_sources(hikari_ui PRIVATE ycbcr_node.cpp ycbcr_node.h)
target_sources(hikari_ui PRIVATE search_suggestions_view.cpp search_suggestions_view.h cited_answer.cpp cited_answer.h)
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
list(APPEND hikari_ui_qml_files AutomationNotices.qml)
list(APPEND hikari_ui_qml_files AutomationResetDialog.qml)
list(APPEND hikari_ui_qml_files FileChooser.qml FolderChooser.qml)
list(APPEND hikari_ui_qml_files AutomationProgress.qml RunLogLine.qml)
list(APPEND hikari_ui_qml_files ScriptResolutionDialog.qml ScriptInfoOnSaveDialog.qml ProjectSettingsPages.qml ProjectAiKeysPage.qml)
list(APPEND hikari_ui_qml_files SubtitleComparisonMenu.qml DocumentTabMenu.qml MoveTabMenu.qml)
list(APPEND hikari_ui_qml_files ReferenceTray.qml)
list(APPEND hikari_ui_qml_files GridToolTip.qml GridScrollBars.qml)
list(APPEND hikari_ui_qml_files QcNotesPanel.qml QcNoteEditor.qml)
list(APPEND hikari_ui_qml_files ExplorerPanel.qml)
list(APPEND hikari_ui_qml_files WindowFooter.qml FooterButton.qml)
list(APPEND hikari_ui_qml_files AssociationOffer.qml TabCommands.qml)
list(APPEND hikari_ui_qml_files UpdateReadyBar.qml UpdateInstallingWindow.qml)
list(APPEND hikari_ui_qml_files FontCollectorDialog.qml)
list(APPEND hikari_ui_qml_files KaraokeCheckDialog.qml)
list(APPEND hikari_ui_qml_files KanjiTimerDialog.qml)
list(APPEND hikari_ui_qml_files AlignToVideoDialog.qml)
list(APPEND hikari_ui_qml_files TrackColourDialog.qml FindFadeDialog.qml VideoPointPicker.qml)
list(APPEND hikari_ui_qml_files LyricsImportDialog.qml)
list(APPEND hikari_ui_qml_files ExportDialog.qml)
list(APPEND hikari_ui_qml_files SettingsImportDialog.qml SettingsImportReview.qml)
list(APPEND hikari_ui_qml_files GettingStartedDialog.qml)
list(APPEND hikari_ui_qml_files FontCatalogBar.qml FontCatalogWindow.qml FontsFromSubtitlesDialog.qml)
set_source_files_properties(FontWindows.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
list(APPEND hikari_ui_qml_files FilterChip.qml FontResolutionNote.qml FontWindows.qml)
list(APPEND hikari_ui_qml_files MatroskaSubtitles.qml)
list(APPEND hikari_ui_qml_files LineInspector.qml LineCounters.qml LineEditorStrip.qml ReviewStrip.qml ReviewPanel.qml AgentPanel.qml InlineAiMenu.qml InlineAiBox.qml InlineAiBar.qml AlignmentChoice.qml TimeField.qml
    NumberField.qml)
list(APPEND hikari_ui_qml_files VisualToolRail.qml VisualOverlay.qml VisualToolBar.qml VisualPointerReadout.qml VisualValueField.qml VisualTagPicker.qml VisualLastChange.qml)
list(APPEND hikari_ui_qml_files VisualToolOption.qml VisualToolDialogs.qml)
list(APPEND hikari_ui_qml_files ShapesEditionDialog.qml ShapeDrawing.qml FieldError.qml)
list(APPEND hikari_ui_qml_files AllTagsEditionDialog.qml)
list(APPEND hikari_ui_qml_files RecentFilesMenu.qml VideoStreamsMenu.qml VideoChaptersMenu.qml
    VideoIndexingProgress.qml VideoFileQuestion.qml DummyVideoDialog.qml VideoDetailsDialog.qml)
list(APPEND hikari_ui_qml_files LayoutSwitcher.qml LayoutMenu.qml SaveLayoutDialog.qml
    ManageLayoutsDialog.qml)
list(APPEND hikari_ui_qml_files PluginManagerDialog.qml PluginWidget.qml PluginList.qml PluginCanvas.qml PluginPanelBody.qml
    PluginDialog.qml PluginFilePicker.qml)
target_sources(hikari_ui PRIVATE status_bar_controller.cpp status_bar_controller.h menu_key.cpp menu_key.h)
set_source_files_properties(StatusHelp.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
list(APPEND hikari_ui_qml_files StatusBar.qml StatusHelp.qml)
set_source_files_properties(MenuKeys.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
list(APPEND hikari_ui_qml_files MenuKeys.qml TextMenu.qml TextMenuItem.qml)
target_sources(hikari_ui PRIVATE icon_theme.cpp icon_theme.h)
target_link_libraries(hikari_ui PRIVATE Qt6::Svg)
file(GLOB hikari_icon_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/*.svg")
list(APPEND hikari_ui_qml_files Icon.qml IconButton.qml
    IconTabButton.qml IconDialogHeader.qml IconToolButton.qml IconTextButton.qml)
list(APPEND hikari_ui_qml_resources icons/manifest.json ${hikari_icon_files})
target_sources(hikari_ui PRIVATE theme.cpp theme.h)
target_sources(hikari_ui PRIVATE title_bars.cpp title_bars.h)
if(WIN32)
    target_link_libraries(hikari_ui PRIVATE dwmapi)
endif()
list(APPEND hikari_ui_qml_files SearchSuggestions.qml AppearancePage.qml SettingsAiPage.qml SettingsSection.qml SettingsRow.qml FocusRing.qml PanelFocusMark.qml DockSeparator.qml DockGroup.qml DockFloatingWindow.qml)
add_subdirectory(style)
add_subdirectory(plugin_api)
target_link_libraries(hikari_ui PUBLIC hikari_ui_styleplugin)
target_sources(hikari_ui PRIVATE video_view_controller.cpp video_view_controller.h)
target_sources(hikari_ui PRIVATE video_fullscreen_controller.cpp video_fullscreen_controller.h)
list(APPEND hikari_ui_qml_files VideoFullscreen.qml FullscreenBarMenu.qml)
list(APPEND hikari_ui_qml_files VideoContextMenu.qml AspectRatioDialog.qml VideoZoomFrame.qml
    VideoRenderCost.qml
    VideoTemplateBadge.qml
    VideoNodeBadge.qml
    VideoVolumeSlider.qml)
list(APPEND hikari_ui_qml_files VideoFollowChoices.qml)
list(APPEND hikari_ui_qml_files AudioOutputMenu.qml MenuRadioItem.qml MenuHeading.qml)
list(APPEND hikari_ui_qml_files LineBreaksMenu.qml ChangeCaseMenu.qml)
list(APPEND hikari_ui_qml_files AudioSliderRow.qml)
list(APPEND hikari_ui_qml_files AudioTapBar.qml TapCalibrationWindow.qml Keycap.qml KeycapSentence.qml)
list(APPEND hikari_ui_qml_files TranslationToggles.qml OriginalTextTools.qml RunStrip.qml TranslationModeQuestion.qml
    TranslationModeOffConfirm.qml)
target_sources(hikari_ui PRIVATE screen_sampler.cpp screen_sampler.h)
list(APPEND hikari_ui_qml_files ScreenDropperView.qml)
list(APPEND hikari_ui_qml_files ColourPopover.qml ColourSwatchGrid.qml)
target_sources(hikari_ui PRIVATE font_faces.cpp font_faces.h font_family_filter.cpp font_family_filter.h)
list(APPEND hikari_ui_qml_files FontFamilyRow.qml FontFamilyBox.qml)
if(UNIX AND NOT APPLE)
    find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS DBus)
    target_link_libraries(hikari_ui PRIVATE Qt6::DBus)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_SCREEN_PORTAL)
endif()
if(UNIX AND NOT APPLE)
    target_sources(hikari_ui PRIVATE portal_colour_scheme.cpp portal_colour_scheme.h)
    target_compile_definitions(hikari_ui PRIVATE HIKARI_COLOUR_SCHEME_PORTAL)
endif()
list(APPEND hikari_ui_qml_files DockMenuButton.qml DockDropHighlight.qml)
# xdg-decoration names xdg-shell's xdg_toplevel, and Qt keeps its own xdg-shell code private, so both are vendored.
target_sources(hikari_ui PRIVATE window_decorations.cpp window_decorations.h)
if(UNIX AND NOT APPLE)
    find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS WaylandClient)
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
target_sources(hikari_ui PRIVATE explorer_model.cpp explorer_model.h)
target_sources(hikari_ui PRIVATE window_frame.cpp window_frame.h window_frame_native.h)
if(WIN32)
    target_sources(hikari_ui PRIVATE window_frame_win.cpp)
    target_link_libraries(hikari_ui PRIVATE dwmapi shell32)
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS WaylandClientPrivate)
    target_link_libraries(hikari_ui PRIVATE Qt6::WaylandClientPrivate ${CMAKE_DL_LIBS})
endif()
list(APPEND hikari_ui_qml_files MainTitleBar.qml MainWindowEdges.qml TitleBarMenus.qml TitleBarMenuItem.qml
    TitleBarMoveArea.qml TitleBarButton.qml)
list(APPEND hikari_ui_qml_files DialogWindow.qml DialogTitleBar.qml AboutDialog.qml CreditsDialog.qml
    UpdateAvailable.qml)
list(APPEND hikari_ui_qml_files LinkButton.qml ReleaseNotesView.qml WhatsNewCard.qml WhatsNewDialog.qml)
list(APPEND hikari_ui_qml_files CrashReportWindow.qml ReportIssueWindow.qml)
list(APPEND hikari_ui_qml_files MessageBox.qml DangerButton.qml Popover.qml PopoverBackground.qml
    ToastStack.qml InlineBar.qml ClashReview.qml ChooseStyles.qml HotkeyCapture.qml ListChooser.qml DatePicker.qml
    MessageHost.qml LogPanel.qml)
list(APPEND hikari_ui_qml_files HidableQuestion.qml)
list(APPEND hikari_ui_qml_files TierChip.qml TierSaveButton.qml TierSaveMenu.qml)
list(APPEND hikari_ui_qml_files SetNewFpsWindow.qml)
list(APPEND hikari_ui_qml_files LazyMessageBox.qml UnsavedChanges.qml Autosaves.qml StartupOffer.qml)
list(APPEND hikari_ui_qml_files SpellingAddedWords.qml ResampleWindow.qml)
list(APPEND hikari_ui_qml_files PerspectiveTrackingWindow.qml TrackingDataField.qml MotionDataWindow.qml InterpolateLinesWindow.qml
    ExtendTrackingWindow.qml BlurAndGlowLayersDialog.qml)
list(APPEND hikari_ui_qml_files RemoveTagsDialog.qml)
list(APPEND hikari_ui_qml_files SplitEveryDialog.qml)
qt_target_qml_sources(hikari_ui QML_FILES ${hikari_ui_qml_files} RESOURCES ${hikari_ui_qml_resources})
set(hikari_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/icons/dawn-peek/app.svg")
set_source_files_properties("${hikari_app_icon}" PROPERTIES QT_RESOURCE_ALIAS "app-icon.svg")
qt_add_resources(hikari_ui "hikari_app_icon" PREFIX "/hikari/ui" FILES "${hikari_app_icon}")
# LibreOffice's hyph_en_US.dic (BSD-style; README_hyph_en_US.txt beside it).
qt_add_resources(hikari_ui "hikari_hyphenation" PREFIX "/hikari/ui" FILES hyphenation/hyph_en_US.dic)
file(GLOB _hikari_karaoke_presets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/application/karaoke-presets/*.ass")
qt_add_resources(hikari_ui "hikari_karaoke_presets" PREFIX "/hikari/ui" BASE "${PROJECT_SOURCE_DIR}/src/application"
    FILES ${_hikari_karaoke_presets})
