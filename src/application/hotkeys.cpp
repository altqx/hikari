#include "hikari/application/hotkeys.h"

#include "hikari/core/ass_load.h"

#include <algorithm>
#include <limits>
#include <array>

namespace hikari::application {

namespace {

constexpr HotkeyAction kActions[] = {
        {"AUDIO_COMMIT_ALT", 1000},
        {"AUDIO_PLAY_ALT", 1001},
        {"AUDIO_PLAY_LINE_ALT", 1002},
        {"AUDIO_PREVIOUS_ALT", 1003},
        {"AUDIO_NEXT_ALT", 1004},
        {"AUDIO_COMMIT", 1010},
        {"AUDIO_PLAY", 1011},
        {"AUDIO_PLAY_LINE", 1012},
        {"AUDIO_PREVIOUS", 1013},
        {"AUDIO_NEXT", 1014},
        {"AUDIO_STOP", 1015},
        {"AUDIO_PLAY_BEFORE_MARK", 1016},
        {"AUDIO_PLAY_AFTER_MARK", 1017},
        {"AUDIO_PLAY_500MS_BEFORE", 1018},
        {"AUDIO_PLAY_500MS_AFTER", 1019},
        {"AUDIO_PLAY_500MS_FIRST", 1020},
        {"AUDIO_PLAY_500MS_LAST", 1021},
        {"AUDIO_PLAY_TO_END", 1022},
        {"AUDIO_SCROLL_LEFT", 1023},
        {"AUDIO_SCROLL_RIGHT", 1024},
        {"AUDIO_GOTO", 1025},
        {"AUDIO_LEAD_IN", 1026},
        {"AUDIO_LEAD_OUT", 1027},
        {"VIDEO_PLAY_PAUSE", 2000},
        {"VIDEO_STOP", 2001},
        {"VIDEO_5_SECONDS_FORWARD", 2002},
        {"VIDEO_5_SECONDS_BACKWARD", 2003},
        {"VIDEO_MINUTE_BACKWARD", 2004},
        {"VIDEO_MINUTE_FORWARD", 2005},
        {"VIDEO_VOLUME_PLUS", 2006},
        {"VIDEO_VOLUME_MINUS", 2007},
        {"VIDEO_PREVIOUS_FILE", 2008},
        {"VIDEO_NEXT_FILE", 2009},
        {"VIDEO_PREVIOUS_CHAPTER", 2010},
        {"VIDEO_NEXT_CHAPTER", 2011},
        {"VIDEO_FULL_SCREEN", 2012},
        {"VIDEO_HIDE_PROGRESS_BAR", 2013},
        {"VIDEO_DELETE_FILE", 2014},
        {"VIDEO_ASPECT_RATIO", 2015},
        {"VIDEO_COPY_COORDS", 2016},
        {"VIDEO_SAVE_FRAME_TO_PNG", 2017},
        {"VIDEO_COPY_FRAME_TO_CLIPBOARD", 2018},
        {"VIDEO_SAVE_SUBBED_FRAME_TO_PNG", 2019},
        {"VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD", 2020},
        {"EDITBOX_CHANGE_FONT", 3000},
        {"EDITBOX_CHANGE_UNDERLINE", 3001},
        {"EDITBOX_CHANGE_STRIKEOUT", 3002},
        {"EDITBOX_PASTE_ALL_TO_TRANSLATION", 3003},
        {"EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 3004},
        {"EDITBOX_HIDE_ORIGINAL", 3005},
        {"EDITBOX_CHANGE_COLOR_PRIMARY", 3006},
        {"EDITBOX_CHANGE_COLOR_SECONDARY", 3007},
        {"EDITBOX_CHANGE_COLOR_OUTLINE", 3008},
        {"EDITBOX_CHANGE_COLOR_SHADOW", 3009},
        {"EDITBOX_COMMIT", 3010},
        {"EDITBOX_COMMIT_GO_NEXT_LINE", 3011},
        {"EDITBOX_INSERT_BOLD", 3012},
        {"EDITBOX_INSERT_ITALIC", 3013},
        {"EDITBOX_SPLIT_LINE", 3014},
        {"EDITBOX_START_DIFFERENCE", 3015},
        {"EDITBOX_END_DIFFERENCE", 3016},
        {"EDITBOX_FIND_NEXT_DOUBTFUL", 3017},
        {"EDITBOX_FIND_NEXT_UNTRANSLATED", 3018},
        {"EDITBOX_SET_DOUBTFUL", 3019},
        {"EDITBOX_TAG_BUTTON1", 3100},
        {"EDITBOX_TAG_BUTTON2", 3101},
        {"EDITBOX_TAG_BUTTON3", 3102},
        {"EDITBOX_TAG_BUTTON4", 3103},
        {"EDITBOX_TAG_BUTTON5", 3104},
        {"EDITBOX_TAG_BUTTON6", 3105},
        {"EDITBOX_TAG_BUTTON7", 3106},
        {"EDITBOX_TAG_BUTTON8", 3107},
        {"EDITBOX_TAG_BUTTON9", 3108},
        {"EDITBOX_TAG_BUTTON10", 3109},
        {"EDITBOX_TAG_BUTTON11", 3110},
        {"EDITBOX_TAG_BUTTON12", 3111},
        {"EDITBOX_TAG_BUTTON13", 3112},
        {"EDITBOX_TAG_BUTTON14", 3113},
        {"EDITBOX_TAG_BUTTON15", 3114},
        {"EDITBOX_TAG_BUTTON16", 3115},
        {"EDITBOX_TAG_BUTTON17", 3116},
        {"EDITBOX_TAG_BUTTON18", 3117},
        {"EDITBOX_TAG_BUTTON19", 3118},
        {"EDITBOX_TAG_BUTTON20", 3119},
        {"GRID_HIDE_LAYER", 4001},
        {"GRID_HIDE_START", 4002},
        {"GRID_HIDE_END", 4004},
        {"GRID_HIDE_ACTOR", 4016},
        {"GRID_HIDE_STYLE", 4008},
        {"GRID_HIDE_MARGINL", 4032},
        {"GRID_HIDE_MARGINR", 4064},
        {"GRID_HIDE_MARGINV", 4128},
        {"GRID_HIDE_EFFECT", 4256},
        {"GRID_HIDE_CPS", 4512},
        {"GRID_HIDE_WRAPS", 4513},
        {"GRID_INSERT_BEFORE", 4514},
        {"GRID_INSERT_AFTER", 4515},
        {"GRID_INSERT_BEFORE_VIDEO", 4516},
        {"GRID_INSERT_AFTER_VIDEO", 4517},
        {"GRID_INSERT_BEFORE_WITH_VIDEO_FRAME", 4518},
        {"GRID_INSERT_AFTER_WITH_VIDEO_FRAME", 4519},
        {"GRID_SELECT_VISIBLE_LINES", 4520},
        {"GRID_SPLIT_BY_VIDEO_TIME", 4521},
        {"GRID_SPLIT_BY_FRAME", 4522},
        {"GRID_SPLIT_BY_CHARS", 4523},
        {"GRID_SPLIT_BY_WORDS", 4524},
        {"GRID_SPLIT_BY_WRAPS", 4525},
        {"GRID_SWAP_LINES", 4526},
        {"GRID_DUPLICATE_LINES", 4527},
        {"GRID_JOIN_LINES", 4528},
        {"GRID_JOIN_TO_FIRST_LINE", 4529},
        {"GRID_JOIN_TO_LAST_LINE", 4530},
        {"GRID_COPY", 4531},
        {"GRID_PASTE", 4532},
        {"GRID_CUT", 4533},
        {"GRID_SHOW_PREVIEW", 4534},
        {"GRID_HIDE_SELECTED", 4535},
        {"GRID_FILTER_BY_NOTHING", 4536},
        {"GRID_FILTER_BY_STYLES", 4537},
        {"GRID_FILTER_BY_SELECTIONS", 4538},
        {"GRID_FILTER_BY_DIALOGUES", 4539},
        {"GRID_FILTER_BY_DOUBTFUL", 4540},
        {"GRID_FILTER_BY_UNTRANSLATED", 4541},
        {"GRID_FILTER", 4542},
        {"GRID_FILTER_AFTER_SUBS_LOAD", 4543},
        {"GRID_FILTER_INVERT", 4544},
        {"GRID_FILTER_DO_NOT_RESET", 4545},
        {"GRID_FILTER_IGNORE_IN_ACTIONS", 4546},
        {"GRID_TREE_MAKE", 4547},
        {"GRID_PASTE_TRANSLATION", 4548},
        {"GRID_TRANSLATION_DIALOG", 4549},
        {"GRID_SUBS_FROM_MKV", 4550},
        {"GRID_MAKE_CONTINOUS_PREVIOUS_LINE", 4551},
        {"GRID_MAKE_CONTINOUS_NEXT_LINE", 4552},
        {"GRID_PASTE_COLUMNS", 4553},
        {"GRID_COPY_COLUMNS", 4554},
        {"GRID_SET_FPS_FROM_VIDEO", 4555},
        {"GRID_SET_NEW_FPS", 4556},
        {"GLOBAL_SAVE_SUBS", 5000},
        {"GLOBAL_SAVE_ALL_SUBS", 5001},
        {"GLOBAL_SAVE_SUBS_AS", 5002},
        {"GLOBAL_SAVE_TRANSLATION", 5003},
        {"GLOBAL_REMOVE_SUBS", 5004},
        {"GLOBAL_REDO", 5005},
        {"GLOBAL_UNDO", 5006},
        {"GLOBAL_UNDO_TO_LAST_SAVE", 5007},
        {"GLOBAL_HISTORY", 5008},
        {"GLOBAL_SEARCH", 5009},
        {"GLOBAL_FIND_REPLACE", 5010},
        {"GLOBAL_FIND_NEXT", 5011},
        {"GLOBAL_MISSPELLS_REPLACER", 5012},
        {"GLOBAL_OPEN_SELECT_LINES", 5013},
        {"GLOBAL_OPEN_SPELLCHECKER", 5014},
        {"GLOBAL_VIDEO_INDEXING", 5015},
        {"GLOBAL_SAVE_WITH_VIDEO_NAME", 5016},
        {"GLOBAL_OPEN_AUDIO", 5017},
        {"GLOBAL_AUDIO_FROM_VIDEO", 5018},
        {"GLOBAL_CLOSE_AUDIO", 5019},
        {"GLOBAL_CONVERT_TO_ASS", 5020},
        {"GLOBAL_CONVERT_TO_SRT", 5021},
        {"GLOBAL_CONVERT_TO_TMP", 5022},
        {"GLOBAL_CONVERT_TO_MDVD", 5023},
        {"GLOBAL_CONVERT_TO_MPL2", 5024},
        {"GLOBAL_OPEN_ASS_PROPERTIES", 5025},
        {"GLOBAL_OPEN_STYLE_MANAGER", 5026},
        {"GLOBAL_OPEN_SUBS_RESAMPLE", 5027},
        {"GLOBAL_OPEN_FONT_COLLECTOR", 5028},
        {"GLOBAL_HIDE_TAGS", 5029},
        {"GLOBAL_SHOW_SHIFT_TIMES", 5030},
        {"GLOBAL_VIEW_ALL", 5031},
        {"GLOBAL_VIEW_AUDIO", 5032},
        {"GLOBAL_VIEW_VIDEO", 5033},
        {"GLOBAL_VIEW_ONLY_VIDEO", 5034},
        {"GLOBAL_VIEW_SUBS", 5035},
        {"GLOBAL_AUTOMATION_LOAD_SCRIPT", 5036},
        {"GLOBAL_AUTOMATION_RELOAD_AUTOLOAD", 5037},
        {"GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT", 5038},
        {"GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW", 5039},
        {"GLOBAL_PLAY_PAUSE", 5040},
        {"GLOBAL_PREVIOUS_FRAME", 5041},
        {"GLOBAL_NEXT_FRAME", 5042},
        {"GLOBAL_VIDEO_ZOOM", 5043},
        {"GLOBAL_RESET_VIDEO_ZOOM", 5044},
        {"GLOBAL_SET_START_TIME", 5045},
        {"GLOBAL_SET_END_TIME", 5046},
        {"GLOBAL_SET_VIDEO_AT_START_TIME", 5047},
        {"GLOBAL_SET_VIDEO_AT_END_TIME", 5048},
        {"GLOBAL_GO_TO_NEXT_KEYFRAME", 5049},
        {"GLOBAL_GO_TO_PREVIOUS_KEYFRAME", 5050},
        {"GLOBAL_SET_AUDIO_FROM_VIDEO", 5051},
        {"GLOBAL_SET_AUDIO_MARK_FROM_VIDEO", 5052},
        {"GLOBAL_LOAD_EXTERNAL_SESSION", 5053},
        {"GLOBAL_SAVE_EXTERNAL_SESSION", 5054},
        {"GLOBAL_LOAD_LAST_SESSION", 5055},
        {"GLOBAL_OPEN_SUBS", 5100},
        {"GLOBAL_OPEN_VIDEO", 5101},
        {"GLOBAL_OPEN_KEYFRAMES", 5102},
        {"GLOBAL_OPEN_DUMMY_VIDEO", 5103},
        {"GLOBAL_OPEN_DUMMY_AUDIO", 5104},
        {"GLOBAL_OPEN_AUTO_SAVE", 5105},
        {"GLOBAL_DELETE_TEMPORARY_FILES", 5106},
        {"GLOBAL_SETTINGS", 5107},
        {"GLOBAL_QUIT", 5108},
        {"GLOBAL_EDITOR", 5109},
        {"GLOBAL_ABOUT", 5110},
        {"GLOBAL_HELPERS", 5111},
        {"GLOBAL_HELP", 5112},
        {"GLOBAL_ANSI", 5113},
        {"GLOBAL_CHECK_FOR_UPDATES", 5114},
        {"GLOBAL_PREVIOUS_LINE", 5150},
        {"GLOBAL_NEXT_LINE", 5151},
        {"GLOBAL_JOIN_WITH_PREVIOUS", 5152},
        {"GLOBAL_JOIN_WITH_NEXT", 5153},
        {"GLOBAL_NEXT_TAB", 5154},
        {"GLOBAL_PREVIOUS_TAB", 5155},
        {"GLOBAL_REMOVE_LINES", 5156},
        {"GLOBAL_REMOVE_TEXT", 5157},
        {"GLOBAL_SNAP_WITH_START", 5158},
        {"GLOBAL_SNAP_WITH_END", 5159},
        {"GLOBAL_SORT_LINES", 5160},
        {"GLOBAL_SORT_SELECTED_LINES", 5161},
        {"GLOBAL_RECENT_AUDIO", 5162},
        {"GLOBAL_RECENT_VIDEO", 5163},
        {"GLOBAL_RECENT_SUBS", 5164},
        {"GLOBAL_RECENT_KEYFRAMES", 5165},
        {"GLOBAL_SELECT_FROM_VIDEO", 5166},
        {"GLOBAL_PLAY_ACTUAL_LINE", 5167},
        {"GLOBAL_STYLE_MANAGER_CLEAN_STYLE", 5168},
        {"GLOBAL_SORT_ALL_BY_START_TIMES", 5200},
        {"GLOBAL_SORT_ALL_BY_END_TIMES", 5201},
        {"GLOBAL_SORT_ALL_BY_STYLE", 5202},
        {"GLOBAL_SORT_ALL_BY_ACTOR", 5203},
        {"GLOBAL_SORT_ALL_BY_EFFECT", 5204},
        {"GLOBAL_SORT_ALL_BY_LAYER", 5205},
        {"GLOBAL_SORT_SELECTED_BY_START_TIMES", 5206},
        {"GLOBAL_SORT_SELECTED_BY_END_TIMES", 5207},
        {"GLOBAL_SORT_SELECTED_BY_STYLE", 5208},
        {"GLOBAL_SORT_SELECTED_BY_ACTOR", 5209},
        {"GLOBAL_SORT_SELECTED_BY_EFFECT", 5210},
        {"GLOBAL_SORT_SELECTED_BY_LAYER", 5211},
        {"GLOBAL_SHIFT_TIMES", 5300},
        {"GLOBAL_ADD_PAGE", 5301},
        {"GLOBAL_CLOSE_PAGE", 5302},
        // HikariSub's own commands, on ids HikariSub legacy left unused.
        {"GLOBAL_EDITOR_HIDE_TAGS", 5169},
        {"GLOBAL_VIDEO_RENDER_COST", 5170},
        {"VIDEO_FULLSCREEN_BAR", 2021},
        {"AUDIO_TAP", 1030},
        {"AUDIO_SPEED_DOWN", 1031},
        {"AUDIO_SPEED_UP", 1032},
        {"GLOBAL_TRANSLATION_MODE", 5171},
        {"EDITBOX_PLACE_RUN_1", 3020},
        {"EDITBOX_PLACE_RUN_2", 3021},
        {"EDITBOX_PLACE_RUN_3", 3022},
        {"EDITBOX_PLACE_RUN_4", 3023},
        {"EDITBOX_PLACE_RUN_5", 3024},
        {"EDITBOX_PLACE_RUN_6", 3025},
        {"EDITBOX_PLACE_RUN_7", 3026},
        {"EDITBOX_PLACE_RUN_8", 3027},
        {"EDITBOX_PLACE_RUN_9", 3028},
        {"EDITBOX_PLACE_NEXT_RUN", 3029},
        // MUST KILL: ids 1005-1009 run as id + 10, so new audio ids start past 1027
        {"AUDIO_KARAOKE_SPLIT", 1028},
        {"AUDIO_KARAOKE_KF", 1029},
        {"VIDEO_TOOL_CROSSHAIR", 2022},
        {"VIDEO_TOOL_POSITION", 2023},
        {"VIDEO_TOOL_MOVE", 2024},
        {"VIDEO_TOOL_SCALE", 2025},
        {"VIDEO_TOOL_ROTATION_Z", 2026},
        {"VIDEO_TOOL_ROTATION_XY", 2027},
        {"VIDEO_TOOL_RECTANGLE_CLIP", 2028},
        {"VIDEO_TOOL_VECTOR_CLIP", 2029},
        {"VIDEO_TOOL_DRAWING", 2030},
        {"VIDEO_TOOL_POSITION_SHIFTER", 2031},
        {"VIDEO_TOOL_HYDRA", 2032},
        {"VIDEO_TOOL_PERSPECTIVE", 2033},
        {"GLOBAL_CHECK_KARAOKE", 5172},
        {"GLOBAL_AUTOMATION_REPEAT_LAST_RUN", 5173},
        {"GLOBAL_KANJI_TIMER", 5174},
        {"GLOBAL_GETTING_STARTED", 5176},
};

struct NameRow {
    int id;
    const char *name;
};
// MUST KILL: raw legacy hotkey ids, no named constants (here and in the defaults)
constexpr NameRow kNames[] = {
        {1010, "Commit"},
        {1000, "Commit alt"},
        {1013, "Previous line"},
        {1003, "Previous line alt"},
        {1014, "Next line"},
        {1004, "Next line alt"},
        {1011, "Play"},
        {1001, "Play alt"},
        {1012, "Play line"},
        {1002, "Play line alt"},
        {1015, "Stop"},
        {1025, "Go to selection"},
        {1024, "Scroll left"},
        {1023, "Scroll right"},
        {1016, "Play before the marker"},
        {1017, "Play after the marker"},
        {1020, "Play first 500ms"},
        {1021, "Play last 500ms"},
        {1018, "Play 500ms before"},
        {1019, "Play 500ms after"},
        {1022, "Play to the end"},
        {1026, "Add lead-in"},
        {1027, "Add lead-out"},
        {3008, "Border color"},
        {3006, "Primary color"},
        {3007, "Secondary color for karaoke"},
        {3009, "Shadow color"},
        {3000, "Font selection"},
        {3002, "Strikethrough"},
        {3001, "Underline"},
        {3010, "Apply changes"},
        {3011, "Apply the changes and go to the next line"},
        {3017, "Next unconfirmed line"},
        {3018, "Next untranslated line"},
        {3005, "Hide original"},
        {3012, "Add bold"},
        {3013, "Add italic"},
        {3003, "Paste all"},
        {3004, "Paste the selected"},
        {3016, "Insert difference to the end"},
        {3019, "Mark as unconfirmed and go to the next line"},
        {3014, "Add line wrap"},
        {3015, "Insert difference from the start"},
        {3100, "First tag button"},
        {3101, "Second tag button"},
        {3102, "Third tag button"},
        {3103, "4th tag button"},
        {3104, "5th tag button"},
        {3105, "6th tag button"},
        {3106, "7th tag button"},
        {3107, "8th tag button"},
        {3108, "9th tag button"},
        {3109, "10th tag button"},
        {3110, "11th tag button"},
        {3111, "12th tag button"},
        {3112, "13th tag button"},
        {3113, "14th tag button"},
        {3114, "15th tag button"},
        {3115, "16th tag button"},
        {3116, "17th tag button"},
        {3117, "18th tag button"},
        {3118, "19th tag button"},
        {3119, "20th tag button"},
        {4552, "Set times as a continuous (next line)"},
        {4551, "Set times as a continuous (previous line)"},
        {4554, "Copy columns"},
        {4527, "Duplicate lines"},
        {4539, "Hide comments"},
        {4540, "Show unconfirmed"},
        {4536, "Turn off filtering"},
        {4538, "Hide selected lines"},
        {4537, "Hide lines with styles"},
        {4541, "Show untranslated"},
        {4555, "Set FPS from video"},
        {4542, "Filter"},
        {4543, "Filter after loading subtitles"},
        {4545, "Do not reset previous filtering"},
        {4546, "Ignore filtering in some actions"},
        {4544, "Reverse filtering"},
        {4016, "Hide actor"},
        {4512, "Hide characters per second"},
        {4004, "Hide end time"},
        {4256, "Hide effect"},
        {4001, "Hide layer"},
        {4032, "Hide left margin"},
        {4064, "Hide right margin"},
        {4128, "Hide vertical margin"},
        {4002, "Hide start time"},
        {4008, "Hide style"},
        {4513, "Hide line wraps"},
        {4547, "Make tree"},
        {4520, "Select all lines visible on video"},
        {4535, "Hide selected lines"},
        {4515, "Insert after"},
        {4517, "Insert after with video time"},
        {4519, "Insert after with video frame time"},
        {4514, "Insert before"},
        {4516, "Insert before with video time"},
        {4518, "Insert before with video frame time"},
        {4528, "Join lines"},
        {4529, "Join lines and keep first"},
        {4530, "Join lines and keep last"},
        {4553, "Paste columns"},
        {4548, "Paste translation text"},
        {4550, "Load subtitles from an MKV file"},
        {4526, "Swap lines"},
        {4549, "Dialogue shifting window"},
        {4556, "Set new FPS"},
        {4534, "Show subtitles preview"},
        {4521, "Split line at video time"},
        {4522, "Split lines into frames"},
        {4523, "Split lines into characters"},
        {4524, "Split lines into words"},
        {4525, "Split lines by wraps"},
        {5025, "ASS file properties"},
        {5114, "Check for updates"},
        {5110, "About"},
        {5113, "Report an issue"},
        {5020, "Convert to ASS"},
        {5021, "Convert to SRT"},
        {5023, "Convert to MDVD"},
        {5024, "Convert to MPL2"},
        {5022, "Convert to TMP"},
        {5018, "Open audio from video"},
        {5036, "Load script"},
        {5037, "Refresh autoload scripts"},
        {5030, "Time shift window"},
        {5019, "Close audio"},
        {5010, "Find and replace"},
        {5028, "Font collector"},
        {5112, "User guide"},
        {5111, "Credits"},
        {5029, "Hide tags"},
        {5008, "History"},
        {5152, "Merge with previous line"},
        {5153, "Merge with next line"},
        {5102, "Open keyframes"},
        {5105, "Open auto save"},
        {5038, "Run the last loaded script"},
        {5200, "Sort all lines by start time"},
        {5201, "Sort all lines by end time"},
        {5202, "Sort all lines by styles"},
        {5203, "Sort all lines by actor"},
        {5204, "Sort all lines by effect"},
        {5205, "Sort all lines by layer"},
        {5206, "Sort selected lines by start time"},
        {5207, "Sort selected lines by end time"},
        {5208, "Sort selected lines by styles"},
        {5209, "Sort selected lines by actor"},
        {5210, "Sort selected lines by effect"},
        {5211, "Sort selected lines by layer"},
        {5300, "Shift times / run time post processor"},
        {5050, "Go to previous keyframe"},
        {5049, "Go to next keyframe"},
        {5027, "Resample subtitles"},
        {5006, "Undo"},
        {5007, "Undo to last save"},
        {5043, "Zoom video"},
        {5044, "Turn off video zoom"},
        {5301, "Open new tab"},
        {5302, "Close current tab"},
        {5168, "Clean styles of ASS file"},
        {5012, "Fix minor errors (experimental)"},
        {5011, "Find next"},
        {5055, "Load last session"},
        {5042, "Next frame"},
        {5151, "Next line"},
        {5154, "Next tab"},
        {5017, "Open audio"},
        {5100, "Open subtitles"},
        {5101, "Open video"},
        {5040, "Play / Pause"},
        {5167, "Play active line"},
        {5041, "Previous frame"},
        {5150, "Previous line"},
        {5155, "Previous tab"},
        {5005, "Redo"},
        {5156, "Delete line"},
        {5004, "Remove subtitles from the editor"},
        {5157, "Delete text"},
        {5001, "Save all subtitles"},
        {5000, "Save"},
        {5002, "Save as..."},
        {5003, "Save translation"},
        {5016, "Save subtitles using the video name"},
        {5009, "Find"},
        {5166, "Select line at current video position"},
        {5013, "Select lines"},
        {5051, "Set audio position to video time"},
        {5052, "Set audio marker to video time"},
        {5046, "Insert end time from video"},
        {5045, "Insert start time from video"},
        {5107, "Settings"},
        {5047, "Go to start time"},
        {5048, "Go to end time of line"},
        {5159, "Change end time to nearest keyframe"},
        {5158, "Change start time to nearest keyframe"},
        {5014, "Check spelling"},
        {5026, "Style manager"},
        {2018, "Copy frame to clipboard"},
        {2014, "Unload video"},
        {2017, "Save frame as PNG"},
        {2003, "5 seconds backward"},
        {2004, "1 minute backward"},
        {2013, "Show / hide progress bar"},
        {2011, "Next chapter"},
        {2009, "Next file"},
        {2000, "Play / Pause"},
        {2015, "Change aspect ratio"},
        {2002, "5 seconds forward"},
        {2005, "1 minute forward"},
        {2010, "Previous chapter"},
        {2008, "Previous file"},
        {2020, "Copy frame with subtitles to clipboard"},
        {2012, "Full screen"},
        {2006, "Volume up"},
        {2007, "Volume down"},
        {2019, "Save frame with subtitles as PNG"},
        {2001, "Stop"},
        {5169, "Hide tags in the line editor"},
        {5170, "Show / hide render cost"},
        {2021, "Full screen bar: Auto / Always / Never"},
        {1030, "Tap to time"},
        {1031, "Slower"},
        {1032, "Faster"},
        {5171, "Translation mode"},
        {3020, "Place run 1"},
        {3021, "Place run 2"},
        {3022, "Place run 3"},
        {3023, "Place run 4"},
        {3024, "Place run 5"},
        {3025, "Place run 6"},
        {3026, "Place run 7"},
        {3027, "Place run 8"},
        {3028, "Place run 9"},
        {3029, "Place the next run"},
        {1028, "Split syllable at the cursor"},
        {1029, "Switch long syllables between \\k and \\kf"},
        {2022, "Position pointer"},
        {2023, "Text positioning"},
        {2024, "Text moving"},
        {2025, "Text scaling"},
        {2026, "Text Z rotation"},
        {2027, "Text X / Y rotation"},
        {2028, "Rectangle clipping"},
        {2029, "Vector clipping"},
        {2030, "Vector drawing"},
        {2031, "Position shifter"},
        {2032, "Hydra"},
        {2033, "Perspective"},
        {5172, "Check karaoke"},
        {5173, "Repeat last run"},
        {5174, "Kanji timer"},
        {5176, "Getting started"},
};

struct DefaultRow {
    int id;
    int type;
    const char *name;
    const char *accel;
};
constexpr DefaultRow kMainDefaults[] = {
        {5108, GlobalHotkey, "Exit", "Alt-F4"},
        {5301, GlobalHotkey, "Open new tab", "Ctrl-T"},
        {5302, GlobalHotkey, "Close current tab", "Ctrl-W"},
        {5030, GlobalHotkey, "Time shift window", "Ctrl-I"},
        {5020, GlobalHotkey, "Convert to ASS", "F9"},
        {5021, GlobalHotkey, "Convert to SRT", "F8"},
        // No F10 for Convert to MDVD: F10 puts the keyboard on the menus.
        {5024, GlobalHotkey, "Convert to MPL2", "F11"},
        {5022, GlobalHotkey, "Convert to TMP", "Ctrl-F12"},
        {5026, GlobalHotkey, "Style manager", "Ctrl-M"},
        {5101, GlobalHotkey, "Open video", "Ctrl-Shift-O"},
        {5009, GlobalHotkey, "Find", "Ctrl-F"},
        {5010, GlobalHotkey, "Find and replace", "Ctrl-H"},
        {5006, GlobalHotkey, "Undo", "Ctrl-Z"},
        {5005, GlobalHotkey, "Redo", "Ctrl-Y"},
        {5008, GlobalHotkey, "History", "Ctrl-Shift-H"},
        {5100, GlobalHotkey, "Open subtitles", "Ctrl-O"},
        {5000, GlobalHotkey, "Save", "Ctrl-S"},
        {5002, GlobalHotkey, "Save as...", "Ctrl-Shift-S"},
        {5157, GlobalHotkey, "Delete text", "Alt-Delete"},
        {5156, GlobalHotkey, "Delete line", "Shift-Delete"},
        {5045, GlobalHotkey, "Insert start time from video", "Ctrl-Left"},
        {5046, GlobalHotkey, "Insert end time from video", "Ctrl-Right"},
        {5040, GlobalHotkey, "Play / Pause", "Alt-Space"},
        {5041, GlobalHotkey, "Previous frame", "Left"},
        {5042, GlobalHotkey, "Next frame", "Right"},
        {5150, GlobalHotkey, "Previous line", "Ctrl-Up"},
        {5151, GlobalHotkey, "Next line", "Ctrl-Down"},
        {5011, GlobalHotkey, "Find next", "F3"},
        {5152, GlobalHotkey, "Merge with previous line", "F4"},
        {5153, GlobalHotkey, "Merge with next line", "F5"},
        {5158, GlobalHotkey, "Change start time to nearest keyframe", "Shift-Left"},
        {5159, GlobalHotkey, "Change end time to nearest keyframe", "Shift-Right"},
        {5154, GlobalHotkey, "Next tab", "Ctrl-PgDn"},
        {5155, GlobalHotkey, "Previous tab", "Ctrl-PgUp"},
        {5166, GlobalHotkey, "Select line at current video position", "F2"},
        {5112, GlobalHotkey, "User guide", "F1"},
        {4527, GridHotkey, "Duplicate lines", "Ctrl-D"},
        {4554, GridHotkey, "Copy columns", "Ctrl-Shift-C"},
        {4553, GridHotkey, "Paste columns", "Ctrl-Shift-V"},
        {4534, GridHotkey, "Show subtitles preview", "Ctrl-Q"},
        {2000, VideoHotkey, "Play / Pause", "Space"},
        {2002, VideoHotkey, "5 seconds forward", "L"},
        {2003, VideoHotkey, "5 seconds backward", ";"},
        {2005, VideoHotkey, "1 minute forward", "Up"},
        {2004, VideoHotkey, "1 minute backward", "Down"},
        {2009, VideoHotkey, "Next file", "."},
        {2008, VideoHotkey, "Previous file", ","},
        {2006, VideoHotkey, "Volume up", "Num ."},
        {2007, VideoHotkey, "Volume down", "Num 0"},
        {2011, VideoHotkey, "Next chapter", "M"},
        {2021, VideoHotkey, "Full screen bar: Auto / Always / Never", "Tab"},
        {2010, VideoHotkey, "Previous chapter", "N"},
        {2022, VideoHotkey, "Position pointer", "X"},
        {2023, VideoHotkey, "Text positioning", "P"},
        {2024, VideoHotkey, "Text moving", "G"},
        {2025, VideoHotkey, "Text scaling", "S"},
        {2026, VideoHotkey, "Text Z rotation", "R"},
        {2027, VideoHotkey, "Text X / Y rotation", "T"},
        {2028, VideoHotkey, "Rectangle clipping", "C"},
        {2029, VideoHotkey, "Vector clipping", "V"},
        {2030, VideoHotkey, "Vector drawing", "D"},
        {2031, VideoHotkey, "Position shifter", "A"},
        {2032, VideoHotkey, "Hydra", "H"},
        {2033, VideoHotkey, "Perspective", "Q"},
        {3012, EditorHotkey, "Add bold", "Ctrl-B"},
        {3013, EditorHotkey, "Add italic", "Ctrl-I"},
        {3014, EditorHotkey, "Add line wrap", "Shift-Enter"},
        {3015, EditorHotkey, "Insert difference from the start", "Ctrl-,"},
        {3016, EditorHotkey, "Insert difference to the end", "Ctrl-."},
        {3017, EditorHotkey, "Next unconfirmed line", "Ctrl-D"},
        {3018, EditorHotkey, "Next untranslated line", "Ctrl-R"},
        {3019, EditorHotkey, "Mark as unconfirmed and go to the next line", "Alt-Down"},
        {3020, EditorHotkey, "Place run 1", "Alt-1"},
        {3021, EditorHotkey, "Place run 2", "Alt-2"},
        {3022, EditorHotkey, "Place run 3", "Alt-3"},
        {3023, EditorHotkey, "Place run 4", "Alt-4"},
        {3024, EditorHotkey, "Place run 5", "Alt-5"},
        {3025, EditorHotkey, "Place run 6", "Alt-6"},
        {3026, EditorHotkey, "Place run 7", "Alt-7"},
        {3027, EditorHotkey, "Place run 8", "Alt-8"},
        {3028, EditorHotkey, "Place run 9", "Alt-9"},
        {3010, EditorHotkey, "Apply changes", "Ctrl-Enter"},
        {3011, EditorHotkey, "Apply the changes and go to the next line", "Enter"},
};
constexpr DefaultRow kAudioDefaults[] = {
        {1010, AudioHotkey, "Commit", "Enter"},
        {1000, AudioHotkey, "Commit alt", "G"},
        {1013, AudioHotkey, "Previous line", "Left"},
        {1003, AudioHotkey, "Previous line alt", "Z"},
        {1014, AudioHotkey, "Next line", "Right"},
        {1004, AudioHotkey, "Next line alt", "X"},
        {1011, AudioHotkey, "Play", "Down"},
        {1001, AudioHotkey, "Play alt", "S"},
        {1012, AudioHotkey, "Play line", "Up"},
        {1002, AudioHotkey, "Play line alt", "R"},
        {1015, AudioHotkey, "Stop", "H"},
        {1025, AudioHotkey, "Go to selection", "B"},
        {1024, AudioHotkey, "Scroll left", "A"},
        {1023, AudioHotkey, "Scroll right", "F"},
        {1016, AudioHotkey, "Play before the marker", "Num 0"},
        {1017, AudioHotkey, "Play after the marker", "Num ."},
        {1020, AudioHotkey, "Play first 500ms", "E"},
        {1021, AudioHotkey, "Play last 500ms", "D"},
        {1018, AudioHotkey, "Play 500ms before", "Q"},
        {1019, AudioHotkey, "Play 500ms after", "W"},
        {1022, AudioHotkey, "Play to the end", "T"},
        {1026, AudioHotkey, "Add lead-in", "C"},
        {1027, AudioHotkey, "Add lead-out", "V"},
        {1028, AudioHotkey, "Split syllable at the cursor", "K"},
        {1029, AudioHotkey, "Switch long syllables between \\k and \\kf", "Shift-K"},
        {1030, AudioHotkey, "Tap to time", "Y"},
        {1031, AudioHotkey, "Slower", "["},
        {1032, AudioHotkey, "Faster", "]"},
};

constexpr std::string_view kWindowNames[] = {"Global", "Subtitles", "Editor", "Video", "Audio"};
constexpr std::string_view kWindowLetters = "GSEVA";

std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

// wxACCEL_* values.
constexpr int kAlt = 1, kCtrl = 2, kShift = 4;
int modifiersOf(std::string_view accel)
{
    int m = 0;
    if (accel.find("Alt-") != std::string_view::npos)
        m |= kAlt;
    if (accel.find("Shift-") != std::string_view::npos)
        m |= kShift;
    if (accel.find("Ctrl-") != std::string_view::npos)
        m |= kCtrl;
    return m;
}

std::string keyTextOf(std::string_view accel)
{
    if (accel.ends_with('-'))
        return "-";
    const auto dash = accel.rfind('-');
    return std::string(dash == std::string_view::npos ? accel : accel.substr(dash + 1));
}

// UTF-16 units, as wxString counts them on Windows.
std::pair<std::size_t, char32_t> utf16LengthAndFirst(std::string_view text)
{
    std::size_t units = 0;
    char32_t first = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        char32_t cp = n == 1 ? c : c & (0x7f >> n);
        for (std::size_t k = 1; k < n && i + k < text.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
        if (units == 0)
            first = cp;
        units += cp > 0xffff ? 2 : 1;
        i += n;
    }
    return {units, first};
}

// A key name's code, else the one UTF-16 unit's; 0 is invalid.
int keyCodeOf(const std::string &keyText)
{
    for (const auto &[code, name] : hotkeyKeyNames())
        if (name == keyText)
            return code;
    const auto [units, first] = utf16LengthAndFirst(keyText);
    if (units < 2)
        return static_cast<int>(first); // "" gives 0
    return 0;
}

std::string qtKeyName(int code)
{
    switch (code) {
    case wxk::Back: return "Backspace";
    case wxk::Space: return "Space";
    case wxk::Return: return "Return";
    case wxk::Tab: return "Tab";
    case wxk::Pause: return "Pause";
    case wxk::Left: return "Left";
    case wxk::Right: return "Right";
    case wxk::Up: return "Up";
    case wxk::Down: return "Down";
    case wxk::Insert: return "Ins";
    case wxk::Delete: return "Del";
    case wxk::Home: return "Home";
    case wxk::End: return "End";
    case wxk::PageUp: return "PgUp";
    case wxk::PageDown: return "PgDown";
    case wxk::NumpadAdd: return "Num++";
    case wxk::NumpadSubtract: return "Num+-";
    case wxk::NumpadMultiply: return "Num+*";
    case wxk::NumpadDivide: return "Num+/";
    case wxk::NumpadDecimal: return "Num+.";
    case wxk::NumpadEnter: return "Num+Enter";
    case wxk::NumpadSeparator: return {}; // Qt has no separator key
    default: break;
    }
    if (code >= wxk::Numpad0 && code <= wxk::Numpad0 + 9)
        return "Num+" + std::to_string(code - wxk::Numpad0);
    if (code >= wxk::F1 && code <= wxk::F1 + 11)
        return "F" + std::to_string(code - wxk::F1 + 1);
    if (code > 32 && code < 127)
        return std::string(1, static_cast<char>(code));
    return {};
}

bool inVideoPlayRange(int id)
{
    return id >= kVideoPlayPause && id <= kVideo5SecondsBackward;
}

std::string reportName(const HotkeyId &key, const Hotkey &hotkey)
{
    auto name = hotkeyName(key.id);
    return name.empty() ? hotkey.name : name;
}

struct ConflictScan {
    bool doubled = false;
    std::string names;
    std::vector<std::string> commands; // the commands' names alone
    bool audioNamed = false;
};

} // namespace

std::span<const HotkeyAction> hotkeyActions()
{
    return kActions;
}

int hotkeyIdOf(std::string_view symbol)
{
    for (const auto &a : kActions)
        if (a.symbol == symbol)
            return a.id;
    return 0;
}

std::string_view hotkeySymbol(int id)
{
    for (const auto &a : kActions)
        if (a.id == id)
            return a.symbol;
    return {};
}

const std::map<int, std::string> &hotkeyNames()
{
    static const std::map<int, std::string> names = [] {
        std::map<int, std::string> out;
        for (const auto &n : kNames)
            out[n.id] = n.name;
        return out;
    }();
    return names;
}

bool isRetiredHotkey(int id)
{
    // MUST KILL: raw retired hotkey ids
    return id == 5015
           || id == 5039
           || id == 5104
           || (id >= 5031 && id <= 5035)
           || id == 5109;
}

std::vector<std::pair<HotkeyId, Hotkey>> dropRetiredHotkeys(HotkeyMap &map)
{
    std::vector<std::pair<HotkeyId, Hotkey>> dropped;
    for (auto it = map.begin(); it != map.end();) {
        if (isRetiredHotkey(it->first.id)) {
            dropped.emplace_back(it->first, it->second);
            it = map.erase(it);
        } else {
            ++it;
        }
    }
    return dropped;
}

std::string hotkeyName(int id)
{
    const auto &names = hotkeyNames();
    const auto it = names.find(id);
    return it == names.end() ? std::string() : it->second;
}

int hotkeyType(int id)
{
    if (id < kVideoPlayPause)
        return AudioHotkey;
    if (id < kEditorFirst)
        return VideoHotkey;
    if (id < kGridFirst || (id >= kEditorTagButton1 && id <= kEditorTagButton20))
        return EditorHotkey;
    if (id < kGlobalFirst)
        return GridHotkey;
    return GlobalHotkey;
}

std::string_view hotkeyWindowName(int type)
{
    return type >= 0 && type < kHotkeyWindows ? kWindowNames[type] : std::string_view();
}

void loadDefaultHotkeys(HotkeyMap &map, bool audio)
{
    for (const auto &d : audio ? std::span<const DefaultRow>(kAudioDefaults) : std::span<const DefaultRow>(kMainDefaults))
        map[HotkeyId{d.id, d.type}] = Hotkey{d.name, d.accel};
}

HotkeyMap defaultHotkeys()
{
    HotkeyMap out;
    loadDefaultHotkeys(out, false);
    loadDefaultHotkeys(out, true);
    return out;
}

std::string defaultHotkey(const HotkeyId &key)
{
    const auto defaults = defaultHotkeys();
    const auto it = defaults.find(key);
    return it == defaults.end() ? std::string() : it->second.accel;
}

std::vector<std::string> hotkeyLines(const HotkeyMap &map, bool audio)
{
    std::vector<std::string> out;
    for (const auto &[key, hotkey] : map) {
        if ((!audio && key.type == AudioHotkey) || (audio && key.type != AudioHotkey) || key.id < 100 ||
            hotkey.accel.empty())
            continue;
        if (key.id >= kFirstScriptHotkey)
            continue; // the automation hotkeys'
        if (key.type < 0 || key.type >= kHotkeyWindows)
            continue;
        std::string symbol(hotkeySymbol(key.id));
        if (symbol.empty())
            symbol = std::to_string(key.id);
        out.push_back(symbol + " " + kWindowLetters[std::size_t(key.type)] + "=" + hotkey.accel);
    }
    return out;
}

int hotkeyLabelNumber(std::string_view label, bool windows)
{
    if (!windows)
        return static_cast<int>(core::legacy::atoi(
            std::u8string_view(reinterpret_cast<const char8_t *>(label.data()), label.size())));
    // Windows _wtoi: leading blanks, a sign, digits; clamps to INT_MAX / INT_MIN.
    std::size_t i = 0;
    while (i < label.size() && (label[i] == ' ' || (label[i] >= '\t' && label[i] <= '\r')))
        ++i;
    bool negative = false;
    if (i < label.size() && (label[i] == '+' || label[i] == '-'))
        negative = label[i++] == '-';
    constexpr std::int64_t limit = std::int64_t(std::numeric_limits<int>::max()) + 1;
    std::int64_t magnitude = 0;
    for (; i < label.size() && label[i] >= '0' && label[i] <= '9'; ++i)
        magnitude = std::min<std::int64_t>(magnitude * 10 + (label[i] - '0'), limit);
    if (negative)
        return magnitude >= limit ? std::numeric_limits<int>::min() : -int(magnitude);
    return magnitude >= limit ? std::numeric_limits<int>::max() : int(magnitude);
}

void readHotkeyLines(HotkeyMap &map, const std::vector<std::string> &lines)
{
#ifdef _WIN32
    readHotkeyLines(map, lines, true);
#else
    readHotkeyLines(map, lines, false);
#endif
}

void readHotkeyLines(HotkeyMap &map, const std::vector<std::string> &lines, bool windows)
{
    for (const auto &line : lines) {
        const std::string token = trim(line);
        if (token.empty() || token.starts_with("Script"))
            continue;
        const auto space = token.find(' ');
        std::string values = space == std::string::npos ? std::string() : trim(token.substr(space + 1));
        const std::string label = trim(token.substr(0, space));
        const auto letter = values.empty() ? std::string::npos : kWindowLetters.find(values[0]);
        const int type = letter == std::string::npos ? -1 : int(letter);
        values = values.size() > 2 ? values.substr(2) : std::string();
        if (values.empty())
            continue;
        // wxString::IsNumber: an optional sign, then digits; "" counts too.
        std::string_view digits = label;
        if (!digits.empty() && (digits.front() == '-' || digits.front() == '+'))
            digits.remove_prefix(1);
        const bool number = std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
        const int id = number ? hotkeyLabelNumber(label, windows) : hotkeyIdOf(label);
        map[HotkeyId{id, type}] = Hotkey{std::string(), values};
    }
}

const std::map<int, std::string> &hotkeyKeyNames()
{
    static const std::map<int, std::string> names = [] {
        std::map<int, std::string> k;
        k[wxk::Back] = "Backspace";
        k[wxk::Space] = "Space";
        k[wxk::Return] = "Enter";
        k[wxk::Tab] = "Tab";
        k[wxk::Pause] = "Pause";
        k[wxk::Left] = "Left";
        k[wxk::Right] = "Right";
        k[wxk::Up] = "Up";
        k[wxk::Down] = "Down";
        k[wxk::Insert] = "Insert";
        k[wxk::Delete] = "Delete";
        k[wxk::Home] = "Home";
        k[wxk::End] = "End";
        k[wxk::PageUp] = "PgUp";
        k[wxk::PageDown] = "PgDn";
        for (int i = 0; i < 10; ++i)
            k[wxk::Numpad0 + i] = "Num " + std::to_string(i);
        k[wxk::NumpadAdd] = "Num +";
        k[wxk::NumpadSubtract] = "Num -";
        k[wxk::NumpadSeparator] = "Num |";
        k[wxk::NumpadMultiply] = "Num *";
        k[wxk::NumpadDivide] = "Num /";
        k[wxk::NumpadDecimal] = "Num .";
        k[wxk::NumpadEnter] = "Num Enter";
        for (int i = 0; i < 12; ++i)
            k[wxk::F1 + i] = "F" + std::to_string(i + 1);
        return k;
    }();
    return names;
}

std::string invalidHotkeyKey(std::string_view accel)
{
    if (accel.empty())
        return {};
    std::string key = keyTextOf(accel);
    for (const auto &[code, name] : hotkeyKeyNames())
        if (name == key)
            return {};
    return utf16LengthAndFirst(key).first >= 2 ? key : std::string();
}

std::string qtKeysOfAccel(std::string_view accel)
{
    if (accel.empty())
        return {};
    const int code = keyCodeOf(keyTextOf(accel));
    const std::string key = code ? qtKeyName(code) : std::string();
    if (key.empty())
        return {};
    const int m = modifiersOf(accel);
    std::string out;
    if (m & kCtrl)
        out += "Ctrl+";
    if (m & kAlt)
        out += "Alt+";
    if (m & kShift)
        out += "Shift+";
    return out + key;
}

std::string accelOfQtKeys(std::string_view keys)
{
    if (keys.empty())
        return {};
    // "Ctrl++" ends with the '+' key; "Num++" is the keypad's.
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < keys.size()) {
        auto plus = keys.find('+', start);
        if (plus == start) {
            parts.emplace_back("+");
            break;
        }
        if (plus == std::string_view::npos) {
            parts.emplace_back(keys.substr(start));
            break;
        }
        parts.emplace_back(keys.substr(start, plus - start));
        start = plus + 1;
    }
    std::string mods;
    bool alt = false, ctrl = false, shift = false, num = false;
    std::string meta;
    std::string key;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto &p = parts[i];
        if (i + 1 < parts.size()) {
            if (p == "Alt")
                alt = true;
            else if (p == "Ctrl")
                ctrl = true;
            else if (p == "Shift")
                shift = true;
            else if (p == "Num")
                num = true;
            else
                meta += p + "-"; // Meta has no accelerator modifier
        } else {
            key = p;
        }
    }
    if (num) {
        if (key == "Enter")
            key = "Num Enter";
        else
            key = "Num " + key;
    } else if (key == "Return" || key == "Enter") {
        key = "Enter";
    } else if (key == "Del") {
        key = "Delete";
    } else if (key == "Ins") {
        key = "Insert";
    } else if (key == "PgDown") {
        key = "PgDn";
    }
    std::string out = meta;
    if (alt)
        out += "Alt-";
    if (ctrl)
        out += "Ctrl-";
    if (shift)
        out += "Shift-";
    return out + key;
}

HotkeyCapture captureHotkey(const HotkeyPress &press, int type)
{
    HotkeyCapture out;
    const int key = press.key;
    if (key == wxk::Shift || key == wxk::Alt || key == wxk::Control)
        return out;
    std::string hotkey;
    if (press.alt)
        hotkey += "Alt-";
    if (press.ctrl)
        hotkey += "Ctrl-";
    if (press.shift)
        hotkey += "Shift-";
    if (hotkey.empty() && (type == GlobalHotkey || type == EditorHotkey) && key > 30 && key < 127) {
        out.result = HotkeyCapture::Result::Refused;
        out.message = "Global and editor shortcuts must include modifiers (e.g. Shift, Ctrl, Alt).";
        return out;
    }
    if (press.onlyCtrl && (key == 'V' || key == 'C' || key == 'X' || key == 'Z')) {
        out.result = HotkeyCapture::Result::Refused;
        out.message = "You cannot use shortcuts for copying, cutting, and pasting.";
        return out;
    }
    if (press.alt && !press.ctrl && key == wxk::F4) {
        out.result = HotkeyCapture::Result::Refused;
        out.message = "You cannot use the program exit shortcut.";
        return out;
    }
    const auto &names = hotkeyKeyNames();
    const auto it = names.find(key);
    std::string keyText = it == names.end() ? std::string() : it->second;
    if (keyText.empty()) {
        if (key >= 36 && key <= 96)
            keyText = std::string(1, static_cast<char>(key));
        else
            return out;
    }
    out.result = HotkeyCapture::Result::Accepted;
    out.accel = hotkey + keyText;
    return out;
}

bool textFieldOwnsKey(std::string_view accel, bool allowNumpadHotkeys)
{
    struct Entry {
        int modifiers;
        int key;
    };
    static constexpr Entry kEntries[] = {
        {0, wxk::Delete}, {0, wxk::Back}, {kCtrl, wxk::Back}, {kCtrl, wxk::Delete},
        {0, wxk::Left}, {0, wxk::Right}, {0, wxk::Up}, {0, wxk::Down},
        {kCtrl, wxk::Left}, {kCtrl, wxk::Right}, {kShift, wxk::Left}, {kShift, wxk::Right},
        {kShift, wxk::Up}, {kShift, wxk::Down}, {kShift | kCtrl, wxk::Left}, {kShift | kCtrl, wxk::Right},
        {kCtrl, 'A'}, {kCtrl, 'V'}, {kCtrl, 'C'}, {kCtrl, 'X'}, {0, wxk::Return},
    };
    if (accel.empty())
        return false;
    const int m = modifiersOf(accel);
    const int code = keyCodeOf(keyTextOf(accel));
    for (const auto &e : kEntries)
        if (e.modifiers == m && e.key == code)
            return true;
    return !allowNumpadHotkeys && m == 0 && code >= wxk::Numpad0 && code <= wxk::Numpad0 + 9;
}

void HotkeyList::clear()
{
    m_history.clear();
    m_iter = 0;
    m_rows.clear();
    m_filtered.clear();
}

void HotkeyList::append(std::string text, std::string name, std::string accel, HotkeyId key)
{
    auto row = std::make_shared<Row>();
    row->text = std::move(text);
    row->name = std::move(name);
    row->accel = std::move(accel);
    row->key = key;
    m_rows.push_back(row);
    m_filtered.push_back(row);
}

void HotkeyList::build(const HotkeyMap &live)
{
    HotkeyMap mapped = live;
    const auto &names = hotkeyNames();
    int lastType = -1;
    // MUST KILL: unnamed bindings follow their window's names, minus GLOBAL_QUIT
    for (auto cur = names.rbegin(); cur != names.rend(); ++cur) {
        const int htype = hotkeyType(cur->first);
        auto next = cur;
        if ((lastType != htype && lastType != -1) || ++next == names.rend()) {
            std::size_t done = 0;
            for (auto it = mapped.begin(); it != mapped.end(); ++it) {
                if (lastType != it->first.type)
                    break;
                ++done;
                if (it->first.id == kGlobalQuit)
                    continue;
                const auto named = names.find(it->first.id);
                const std::string name = named != names.end() ? named->second : it->second.name;
                append(std::string(hotkeyWindowName(it->first.type)) + " " + name, name, it->second.accel, it->first);
            }
            for (std::size_t i = 0; i < done; ++i)
                mapped.erase(mapped.begin());
        }
        if (mapped.empty())
            break;
        std::string accel;
        const auto it = mapped.find(HotkeyId{cur->first, htype});
        if (it != mapped.end()) {
            accel = it->second.accel;
            mapped.erase(it);
        }
        append(std::string(hotkeyWindowName(htype)) + " " + cur->second, cur->second, accel,
               HotkeyId{cur->first, htype});
        lastType = htype;
    }
    m_history.push_back(m_rows);
    m_sel = findShown(0);
}

std::vector<int> HotkeyList::shown() const
{
    std::vector<int> out;
    for (const auto &r : m_filtered) {
        const auto it = std::ranges::find(m_rows, r);
        if (it != m_rows.end())
            out.push_back(int(it - m_rows.begin()));
    }
    return out;
}

namespace {

bool shownByFilter(int mode, const HotkeyList::Row &r)
{
    switch (mode) {
    case 1: return !r.accel.empty();
    case 2: return r.key.type == GlobalHotkey;
    case 3: return r.key.type == GridHotkey;
    case 4: return r.key.type == EditorHotkey;
    case 5: return r.key.type == VideoHotkey;
    case 6: return r.key.type == AudioHotkey;
    case 7: return r.key.id >= kFirstScriptHotkey;
    default: return true;
    }
}

} // namespace

void HotkeyList::addRows(const HotkeyMap &more)
{
    const RowPtr selected = m_sel >= 0 && std::size_t(m_sel) < m_filtered.size() ? m_filtered[std::size_t(m_sel)] : nullptr;
    for (const auto &[key, hotkey] : more) {
        auto row = std::make_shared<Row>();
        row->text = std::string(hotkeyWindowName(key.type)) + " " + hotkey.name;
        row->name = hotkey.name;
        row->accel = hotkey.accel;
        row->key = key;
        row->visible = shownNow(*row) ? 1 : 0;
        m_isFiltered = m_isFiltered || !row->visible;
        m_rows.push_back(row);
        for (auto &step : m_history)
            step.push_back(row);
    }
    rebuildFiltered();
    if (selected) {
        const auto it = std::ranges::find(m_filtered, selected);
        m_sel = it != m_filtered.end() ? int(it - m_filtered.begin()) : m_sel;
    }
}

void HotkeyList::removeUnchangedRows(const std::function<bool(const Row &)> &gone)
{
    const RowPtr selected = m_sel >= 0 && std::size_t(m_sel) < m_filtered.size() ? m_filtered[std::size_t(m_sel)] : nullptr;
    std::vector<RowPtr> dropped;
    for (const auto &row : m_rows)
        if (gone(*row) && std::ranges::all_of(m_history, [&](const auto &step) { return std::ranges::find(step, row) != step.end(); }))
            dropped.push_back(row);
    if (dropped.empty())
        return;
    // Each step loses the same rows, so the rest stay aligned between steps.
    for (const auto &row : dropped) {
        std::erase(m_rows, row);
        for (auto &step : m_history)
            std::erase(step, row);
    }
    rebuildFiltered();
    const auto it = selected ? std::ranges::find(m_filtered, selected) : m_filtered.end();
    if (it != m_filtered.end())
        m_sel = int(it - m_filtered.begin());
    else if (m_sel >= int(m_filtered.size()))
        m_sel = int(m_filtered.size()) - 1;
}

bool HotkeyList::shownNow(const Row &r) const
{
    if (!shownByFilter(m_filterMode, r))
        return false;
    if (m_search.empty())
        return true;
    const auto holds = [this](std::string_view text) {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
        return lower.find(m_search) != std::string::npos;
    };
    return holds(r.text) || holds(r.accel) || (m_searchAlso && holds(m_searchAlso(r)));
}

void HotkeyList::search(std::string text, const std::function<std::string(const Row &)> &also)
{
    std::ranges::transform(text, text.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    m_search = std::move(text);
    m_searchAlso = also;
    filter(m_filterMode);
}

void HotkeyList::filter(int mode)
{
    m_filterMode = mode;
    m_sel = 0;
    m_isFiltered = false;
    for (auto &r : m_rows) {
        const bool visible = shownNow(*r);
        r->visible = visible ? 1 : 0;
        if (!visible)
            m_isFiltered = true;
    }
    rebuildFiltered();
}

int HotkeyList::findKey(int position) const
{
    if (position < 0 || std::size_t(position) >= m_filtered.size())
        return -1;
    const auto &r = m_filtered[std::size_t(position)];
    for (std::size_t i = std::size_t(position); i < m_rows.size(); ++i)
        if (m_rows[i] == r)
            return int(i);
    return -1;
}

int HotkeyList::hiddenBlock(int position) const
{
    const int actual = findKey(position);
    const int plusOne = findKey(position + 1);
    const int plusOneSafe = plusOne < 0 ? int(m_rows.size()) : plusOne;
    if (actual + 1 < plusOneSafe)
        return 1;
    if (plusOne < 0)
        return 0;
    if ((actual < 0 || m_rows[std::size_t(actual)]->visible != 2) && m_rows[std::size_t(plusOne)]->visible == 2)
        return 2;
    return 0;
}

void HotkeyList::toggleBlock(int position)
{
    if (!m_isFiltered || !hiddenBlock(position))
        return;
    for (std::size_t i = std::size_t(findKey(position) + 1); i < m_rows.size(); ++i) {
        auto &r = m_rows[i];
        if (r->visible == 0)
            r->visible = 2;
        else if (r->visible == 2)
            r->visible = 0;
        else
            break;
    }
    rebuildFiltered();
}

void HotkeyList::rebuildFiltered()
{
    m_filtered.clear();
    for (const auto &r : m_rows)
        if (r->visible)
            m_filtered.push_back(r);
}

int HotkeyList::selection() const
{
    if (m_sel < 0 || std::size_t(m_sel) >= m_filtered.size())
        return -1;
    const auto &r = m_filtered[std::size_t(m_sel)];
    for (std::size_t i = std::size_t(m_sel); i < m_rows.size(); ++i)
        if (m_rows[i] == r)
            return int(i);
    return -1;
}

void HotkeyList::selectShown(int position)
{
    m_sel = position;
}

int HotkeyList::findShown(int row) const
{
    if (row < 0 || std::size_t(row) >= m_rows.size())
        return -1;
    const auto &r = m_rows[std::size_t(row)];
    if (!r->visible || m_filtered.empty())
        return -1;
    for (int i = std::min(row, int(m_filtered.size()) - 1); i >= 0; --i)
        if (m_filtered[std::size_t(i)] == r)
            return i;
    return -1;
}

int HotkeyList::find(const std::string &text) const
{
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i]->text == text)
            return int(i);
    return -1;
}

HotkeyList::Row &HotkeyList::copyRow(int y, bool pushBack)
{
    auto row = std::make_shared<Row>(*m_rows[std::size_t(y)]);
    row->visible = 1;
    if (pushBack) {
        m_rows.push_back(row);
        m_filtered.push_back(row);
        return *row;
    }
    const int id = findShown(y);
    m_rows[std::size_t(y)] = row;
    if (id != -1)
        m_filtered[std::size_t(id)] = row;
    return *row;
}

void HotkeyList::pushHistory()
{
    if (std::size_t(m_iter) != m_history.size() && std::size_t(m_iter) + 1 <= m_history.size())
        m_history.erase(m_history.begin() + m_iter + 1, m_history.end());
    m_history.push_back(m_rows);
    ++m_iter;
}

void HotkeyList::ensureCopy(const HotkeyMap &live)
{
    if (m_copy.empty())
        m_copy = live;
}

std::vector<HotkeyId> HotkeyList::conflicts(int index, const std::string &accel) const
{
    std::vector<HotkeyId> out;
    for (const auto &[key, hotkey] : m_copy)
        if (hotkey.accel == accel && key.id != row(index).key.id)
            out.push_back(key);
    return out;
}

namespace {

HotkeyConflict question(const ConflictScan &scan, std::size_t count)
{
    HotkeyConflict c;
    c.doubled = scan.doubled;
    c.count = count;
    c.commands = scan.commands;
    if (scan.doubled) {
        c.message = "This hotkey already exists for \"" + scan.names + "\".\nWhat to do?";
        c.canSwitch = true;
    } else {
        c.message = "This shortcut already exists in " + std::string(count > 1 ? "other windows" : "another window") +
                    " as a shortcut for \"" + scan.names + "\".\nWhat would you like to do?";
        c.canSwitch = count < 2;
        c.canSetAnyway = true;
    }
    return c;
}

void addName(ConflictScan &scan, const HotkeyId &key, const Hotkey &hotkey)
{
    if (!scan.names.empty())
        scan.names += ", ";
    scan.names += std::string(hotkeyWindowName(key.type)) + " " + reportName(key, hotkey);
    scan.commands.push_back(reportName(key, hotkey));
}

} // namespace

std::optional<HotkeyConflict> HotkeyList::mapConflict(int index, const std::string &accel, int type,
                                                      const HotkeyMap &live)
{
    ensureCopy(live);
    const auto found = conflicts(index, accel);
    if (found.empty())
        return std::nullopt;
    const int id = row(index).key.id;
    ConflictScan scan;
    for (const auto &k : found) {
        // MUST KILL: Video/Audio, and video play/seek ids, count as one window
        if (k.type == type || (k.type >= VideoHotkey && type >= VideoHotkey) ||
            ((inVideoPlayRange(id) || inVideoPlayRange(k.id)) && (k.type >= GridHotkey || type >= GridHotkey))) {
            scan.doubled = true;
            scan.names = reportName(k, m_copy.at(k));
            scan.commands = {scan.names};
            break;
        }
        addName(scan, k, m_copy.at(k));
    }
    return question(scan, found.size());
}

void HotkeyList::map(int index, const std::string &accel, int type, HotkeyAnswer answer, const HotkeyMap &live)
{
    if (const auto c = mapConflict(index, accel, type, live)) {
        if (answer == HotkeyAnswer::Cancel)
            return;
        if (answer == HotkeyAnswer::Switch || answer == HotkeyAnswer::Delete) {
            // Switch gives the conflicts this row's keys; Delete clears them.
            const std::string hotkey = answer == HotkeyAnswer::Delete ? std::string() : row(index).accel;
            for (const auto &k : conflicts(index, accel)) {
                if (c->doubled && k.type != type)
                    continue;
                // MUST KILL: only conflicts found by their listed name change, the copy too
                const int other = find(std::string(hotkeyWindowName(k.type)) + " " + hotkeyName(k.id));
                if (other >= 0) {
                    auto &r = copyRow(other);
                    r.accel = hotkey;
                    r.keyModified = true;
                    r.textModified = true;
                    m_copy[k].accel = hotkey;
                }
            }
        }
    }
    const Row current = row(index);
    if (current.key.type != type) {
        const int other = find(std::string(hotkeyWindowName(type)) + " " + current.name);
        if (other < 0) {
            auto &r = copyRow(index, true);
            r.text = std::string(hotkeyWindowName(type)) + " " + current.name;
            r.textModified = true;
            r.accel = accel;
            r.keyModified = true;
            r.key = HotkeyId{current.key.id, type};
            // MUST KILL: findShown past the end, always -1
            m_sel = findShown(int(m_rows.size()));
        } else {
            auto &r = copyRow(other);
            r.accel = accel;
            r.keyModified = true;
            r.textModified = true;
            m_sel = findShown(other);
        }
        m_copy[HotkeyId{current.key.id, type}] = Hotkey{current.name, accel};
        m_modified = true;
        pushHistory();
        rerunSearch();
        return;
    }
    auto &r = copyRow(index);
    r.accel = accel;
    r.keyModified = true;
    r.textModified = true;
    m_copy[current.key] = Hotkey{current.name, accel};
    m_modified = true;
    pushHistory();
    rerunSearch();
}

void HotkeyList::reset(int index, const HotkeyMap &live)
{
    ensureCopy(live);
    const std::string def = defaultHotkey(row(index).key);
    auto &r = copyRow(index);
    r.accel = def;
    r.keyModified = true;
    r.textModified = true;
    m_modified = true;
    pushHistory();
    m_copy[r.key] = Hotkey{r.name, def};
    rerunSearch();
}

void HotkeyList::resetAll(const HotkeyMap &live)
{
    ensureCopy(live);
    bool changed = false;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const std::string def = defaultHotkey(m_rows[i]->key);
        if (m_rows[i]->accel == def)
            continue;
        const int visible = m_rows[i]->visible; // a row the filter hides stays hidden
        auto &r = copyRow(int(i));
        r.visible = visible;
        r.accel = def;
        r.keyModified = true;
        r.textModified = true;
        m_copy[r.key] = Hotkey{r.name, def};
        changed = true;
    }
    if (!changed)
        return;
    m_modified = true;
    pushHistory();
    rerunSearch();
}

void HotkeyList::remove(int index, const HotkeyMap &live)
{
    ensureCopy(live);
    auto &r = copyRow(index);
    r.accel.clear();
    r.keyModified = true;
    m_modified = true;
    pushHistory();
    m_copy[r.key] = Hotkey{r.name, std::string()};
    rerunSearch();
}

void HotkeyList::moveInHistory(int to)
{
    m_iter = to;
    std::vector<RowPtr> actual = m_history[std::size_t(m_iter)];
    // MUST KILL: rows past the snapshot's end leave the list but stay in the copy
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (i < actual.size() && m_rows[i] != actual[i]) {
            m_copy[actual[i]->key] = Hotkey{actual[i]->name, actual[i]->accel};
            m_modified = true;
        }
    m_rows = std::move(actual);
    rebuildFiltered();
    rerunSearch();
}

void HotkeyList::rerunSearch()
{
    if (m_search.empty())
        return;
    const int selected = selection();
    const std::optional<HotkeyId> key = selected >= 0 ? std::optional(m_rows[std::size_t(selected)]->key) : std::nullopt;
    filter(m_filterMode);
    m_sel = -1;
    if (!key)
        return;
    for (std::size_t i = 0; i < m_filtered.size(); ++i)
        if (m_filtered[i]->key == *key)
            m_sel = int(i);
    if (m_sel < 0 && !m_filtered.empty())
        m_sel = 0; // the edited row left the results
}

void HotkeyList::undo()
{
    if (m_iter > 0)
        moveInHistory(m_iter - 1);
}

void HotkeyList::redo()
{
    if (m_iter < int(m_history.size()) - 1)
        moveInHistory(m_iter + 1);
}

std::optional<HotkeyMap> HotkeyList::commit()
{
    if (!m_modified || m_copy.empty())
        return std::nullopt;
    // Clears the flags in the rows the history shares too.
    for (auto &r : m_rows) {
        r->keyModified = false;
        r->textModified = false;
    }
    m_modified = false;
    return m_copy;
}

namespace {

ConflictScan scanNow(const HotkeyMap &live, const std::vector<HotkeyId> &found, int type)
{
    ConflictScan scan;
    for (const auto &k : found) {
        if (k.type == type) {
            scan.doubled = true;
            scan.names = reportName(k, live.at(k));
            scan.commands = {scan.names};
            break;
        }
        addName(scan, k, live.at(k));
        if (k.type == AudioHotkey)
            scan.audioNamed = true;
    }
    return scan;
}

std::vector<HotkeyId> holding(const HotkeyMap &live, const std::string &accel)
{
    std::vector<HotkeyId> out;
    for (const auto &[key, hotkey] : live)
        if (hotkey.accel == accel)
            out.push_back(key);
    return out;
}

} // namespace

std::optional<HotkeyConflict> mapHotkeyNowConflict(const HotkeyMap &live, int, const std::string &accel, int type)
{
    // Every binding holding the keys, the one being mapped included.
    const auto found = holding(live, accel);
    if (found.empty())
        return std::nullopt;
    return question(scanNow(live, found, type), found.size());
}

HotkeyNowResult mapHotkeyNow(HotkeyMap &live, int id, const std::string &name, const std::string &accel, int type,
                             HotkeyAnswer answer)
{
    HotkeyNowResult out;
    out.saveAudio = type == AudioHotkey;
    const auto found = holding(live, accel);
    if (!found.empty()) {
        const auto scan = scanNow(live, found, type);
        out.saveAudio = out.saveAudio || scan.audioNamed;
        if (answer == HotkeyAnswer::Cancel)
            return out;
        if (answer != HotkeyAnswer::SetAnyway)
            for (const auto &k : found) {
                if (scan.doubled && k.type != type)
                    continue;
                if (answer == HotkeyAnswer::Switch) {
                    // MUST KILL: (id, window) among them clears itself; later ones get nothing
                    live[k].accel.clear();
                    const auto mine = live.find(HotkeyId{id, type});
                    live[k].accel = mine != live.end() ? mine->second.accel : std::string();
                } else {
                    live[k].accel.clear();
                }
            }
    }
    live[HotkeyId{id, type}] = Hotkey{name, accel};
    out.changed = true;
    return out;
}

} // namespace hikari::application
