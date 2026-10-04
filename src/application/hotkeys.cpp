#include "hikari/application/hotkeys.h"

#include "hikari/core/ass_load.h" // core::legacy::atoi (wxAtoi)

#include <algorithm>
#include <limits>
#include <array>

namespace hikari::application {

namespace {

// Hotkeys.h IDS.
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
};

struct NameRow {
    int id;
    const char *name;
};
// HotkeysNaming::CreateNamesMap.
constexpr NameRow kNames[] = {
        {1010, "Commit"}, // AUDIO_COMMIT
        {1000, "Commit alt"}, // AUDIO_COMMIT_ALT
        {1013, "Previous line"}, // AUDIO_PREVIOUS
        {1003, "Previous line alt"}, // AUDIO_PREVIOUS_ALT
        {1014, "Next line"}, // AUDIO_NEXT
        {1004, "Next line alt"}, // AUDIO_NEXT_ALT
        {1011, "Play"}, // AUDIO_PLAY
        {1001, "Play alt"}, // AUDIO_PLAY_ALT
        {1012, "Play line"}, // AUDIO_PLAY_LINE
        {1002, "Play line alt"}, // AUDIO_PLAY_LINE_ALT
        {1015, "Stop"}, // AUDIO_STOP
        {1025, "Go to selection"}, // AUDIO_GOTO
        {1024, "Scroll left"}, // AUDIO_SCROLL_RIGHT
        {1023, "Scroll right"}, // AUDIO_SCROLL_LEFT
        {1016, "Play before the marker"}, // AUDIO_PLAY_BEFORE_MARK
        {1017, "Play after the marker"}, // AUDIO_PLAY_AFTER_MARK
        {1020, "Play first 500ms"}, // AUDIO_PLAY_500MS_FIRST
        {1021, "Play last 500ms"}, // AUDIO_PLAY_500MS_LAST
        {1018, "Play 500ms before"}, // AUDIO_PLAY_500MS_BEFORE
        {1019, "Play 500ms after"}, // AUDIO_PLAY_500MS_AFTER
        {1022, "Play to the end"}, // AUDIO_PLAY_TO_END
        {1026, "Add lead-in"}, // AUDIO_LEAD_IN
        {1027, "Add lead-out"}, // AUDIO_LEAD_OUT
        {3008, "Border color"}, // EDITBOX_CHANGE_COLOR_OUTLINE
        {3006, "Primary color"}, // EDITBOX_CHANGE_COLOR_PRIMARY
        {3007, "Secondary color for karaoke"}, // EDITBOX_CHANGE_COLOR_SECONDARY
        {3009, "Shadow color"}, // EDITBOX_CHANGE_COLOR_SHADOW
        {3000, "Font selection"}, // EDITBOX_CHANGE_FONT
        {3002, "Strikethrough"}, // EDITBOX_CHANGE_STRIKEOUT
        {3001, "Underline"}, // EDITBOX_CHANGE_UNDERLINE
        {3010, "Apply changes"}, // EDITBOX_COMMIT
        {3011, "Apply the changes and go to the next line"}, // EDITBOX_COMMIT_GO_NEXT_LINE
        {3017, "Next unconfirmed line"}, // EDITBOX_FIND_NEXT_DOUBTFUL
        {3018, "Next untranslated line"}, // EDITBOX_FIND_NEXT_UNTRANSLATED
        {3005, "Hide original"}, // EDITBOX_HIDE_ORIGINAL
        {3012, "Add bold"}, // EDITBOX_INSERT_BOLD
        {3013, "Add italic"}, // EDITBOX_INSERT_ITALIC
        {3003, "Paste all"}, // EDITBOX_PASTE_ALL_TO_TRANSLATION
        {3004, "Paste the selected"}, // EDITBOX_PASTE_SELECTION_TO_TRANSLATION
        {3016, "Insert difference to the end"}, // EDITBOX_END_DIFFERENCE
        {3019, "Mark as unconfirmed and go to the next line"}, // EDITBOX_SET_DOUBTFUL
        {3014, "Add line wrap"}, // EDITBOX_SPLIT_LINE
        {3015, "Insert difference from the start"}, // EDITBOX_START_DIFFERENCE
        {3100, "First tag button"}, // EDITBOX_TAG_BUTTON1
        {3101, "Second tag button"}, // EDITBOX_TAG_BUTTON2
        {3102, "Third tag button"}, // EDITBOX_TAG_BUTTON3
        {3103, "4th tag button"}, // EDITBOX_TAG_BUTTON4
        {3104, "5th tag button"}, // EDITBOX_TAG_BUTTON5
        {3105, "6th tag button"}, // EDITBOX_TAG_BUTTON6
        {3106, "7th tag button"}, // EDITBOX_TAG_BUTTON7
        {3107, "8th tag button"}, // EDITBOX_TAG_BUTTON8
        {3108, "9th tag button"}, // EDITBOX_TAG_BUTTON9
        {3109, "10th tag button"}, // EDITBOX_TAG_BUTTON10
        {3110, "11th tag button"}, // EDITBOX_TAG_BUTTON11
        {3111, "12th tag button"}, // EDITBOX_TAG_BUTTON12
        {3112, "13th tag button"}, // EDITBOX_TAG_BUTTON13
        {3113, "14th tag button"}, // EDITBOX_TAG_BUTTON14
        {3114, "15th tag button"}, // EDITBOX_TAG_BUTTON15
        {3115, "16th tag button"}, // EDITBOX_TAG_BUTTON16
        {3116, "17th tag button"}, // EDITBOX_TAG_BUTTON17
        {3117, "18th tag button"}, // EDITBOX_TAG_BUTTON18
        {3118, "19th tag button"}, // EDITBOX_TAG_BUTTON19
        {3119, "20th tag button"}, // EDITBOX_TAG_BUTTON20
        {4552, "Set times as a continuous (next line)"}, // GRID_MAKE_CONTINOUS_NEXT_LINE
        {4551, "Set times as a continuous (previous line)"}, // GRID_MAKE_CONTINOUS_PREVIOUS_LINE
        {4554, "Copy columns"}, // GRID_COPY_COLUMNS
        {4527, "Duplicate lines"}, // GRID_DUPLICATE_LINES
        {4539, "Hide comments"}, // GRID_FILTER_BY_DIALOGUES
        {4540, "Show unconfirmed"}, // GRID_FILTER_BY_DOUBTFUL
        {4536, "Turn off filtering"}, // GRID_FILTER_BY_NOTHING
        {4538, "Hide selected lines"}, // GRID_FILTER_BY_SELECTIONS
        {4537, "Hide lines with styles"}, // GRID_FILTER_BY_STYLES
        {4541, "Show untranslated"}, // GRID_FILTER_BY_UNTRANSLATED
        {4555, "Set FPS from video"}, // GRID_SET_FPS_FROM_VIDEO
        {4542, "Filter"}, // GRID_FILTER
        {4543, "Filter after loading subtitles"}, // GRID_FILTER_AFTER_SUBS_LOAD
        {4545, "Do not reset previous filtering"}, // GRID_FILTER_DO_NOT_RESET
        {4546, "Ignore filtering in some actions"}, // GRID_FILTER_IGNORE_IN_ACTIONS
        {4544, "Reverse filtering"}, // GRID_FILTER_INVERT
        {4016, "Hide actor"}, // GRID_HIDE_ACTOR
        {4512, "Hide characters per second"}, // GRID_HIDE_CPS
        {4004, "Hide end time"}, // GRID_HIDE_END
        {4256, "Hide effect"}, // GRID_HIDE_EFFECT
        {4001, "Hide layer"}, // GRID_HIDE_LAYER
        {4032, "Hide left margin"}, // GRID_HIDE_MARGINL
        {4064, "Hide right margin"}, // GRID_HIDE_MARGINR
        {4128, "Hide vertical margin"}, // GRID_HIDE_MARGINV
        {4002, "Hide start time"}, // GRID_HIDE_START
        {4008, "Hide style"}, // GRID_HIDE_STYLE
        {4547, "Make tree"}, // GRID_TREE_MAKE
        {4520, "Select all lines visible on video"}, // GRID_SELECT_VISIBLE_LINES
        {4535, "Hide selected lines"}, // GRID_HIDE_SELECTED
        {4515, "Insert after"}, // GRID_INSERT_AFTER
        {4517, "Insert after with video time"}, // GRID_INSERT_AFTER_VIDEO
        {4519, "Insert after with video frame time"}, // GRID_INSERT_AFTER_WITH_VIDEO_FRAME
        {4514, "Insert before"}, // GRID_INSERT_BEFORE
        {4516, "Insert before with video time"}, // GRID_INSERT_BEFORE_VIDEO
        {4518, "Insert before with video frame time"}, // GRID_INSERT_BEFORE_WITH_VIDEO_FRAME
        {4528, "Join lines"}, // GRID_JOIN_LINES
        {4529, "Join lines and keep first"}, // GRID_JOIN_TO_FIRST_LINE
        {4530, "Join lines and keep last"}, // GRID_JOIN_TO_LAST_LINE
        {4553, "Paste columns"}, // GRID_PASTE_COLUMNS
        {4548, "Paste translation text"}, // GRID_PASTE_TRANSLATION
        {4550, "Load subtitles from an MKV file"}, // GRID_SUBS_FROM_MKV
        {4526, "Swap lines"}, // GRID_SWAP_LINES
        {4549, "Dialogue shifting window"}, // GRID_TRANSLATION_DIALOG
        {4556, "Set new FPS"}, // GRID_SET_NEW_FPS
        {4534, "Show subtitles preview"}, // GRID_SHOW_PREVIEW
        {4521, "Split line at video time"}, // GRID_SPLIT_BY_VIDEO_TIME
        {4522, "Split lines into frames"}, // GRID_SPLIT_BY_FRAME
        {4523, "Split lines into characters"}, // GRID_SPLIT_BY_CHARS
        {4524, "Split lines into words"}, // GRID_SPLIT_BY_WORDS
        {4525, "Split lines by wraps"}, // GRID_SPLIT_BY_WRAPS
        {5025, "ASS file properties"}, // GLOBAL_OPEN_ASS_PROPERTIES
        {5114, "Check for updates"}, // GLOBAL_CHECK_FOR_UPDATES
        {5110, "About"}, // GLOBAL_ABOUT
        {5113, "Report an issue"}, // GLOBAL_ANSI
        {5020, "Convert to ASS"}, // GLOBAL_CONVERT_TO_ASS
        {5021, "Convert to SRT"}, // GLOBAL_CONVERT_TO_SRT
        {5023, "Convert to MDVD"}, // GLOBAL_CONVERT_TO_MDVD
        {5024, "Convert to MPL2"}, // GLOBAL_CONVERT_TO_MPL2
        {5022, "Convert to TMP"}, // GLOBAL_CONVERT_TO_TMP
        {5018, "Open audio from video"}, // GLOBAL_AUDIO_FROM_VIDEO
        {5036, "Load script"}, // GLOBAL_AUTOMATION_LOAD_SCRIPT
        {5039, "Open shortcut mapping window"}, // GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW
        {5037, "Refresh autoload scripts"}, // GLOBAL_AUTOMATION_RELOAD_AUTOLOAD
        {5030, "Time shift window"}, // GLOBAL_SHOW_SHIFT_TIMES
        {5019, "Close audio"}, // GLOBAL_CLOSE_AUDIO
        {5109, "Enable / Disable editor"}, // GLOBAL_EDITOR
        {5010, "Find and replace"}, // GLOBAL_FIND_REPLACE
        {5028, "Font collector"}, // GLOBAL_OPEN_FONT_COLLECTOR
        {5112, "HikariSub website"}, // GLOBAL_HELP
        {5111, "Credits"}, // GLOBAL_HELPERS
        {5029, "Hide tags"}, // GLOBAL_HIDE_TAGS
        {5008, "History"}, // GLOBAL_HISTORY
        {5152, "Merge with previous line"}, // GLOBAL_JOIN_WITH_PREVIOUS
        {5153, "Merge with next line"}, // GLOBAL_JOIN_WITH_NEXT
        {5102, "Open keyframes"}, // GLOBAL_OPEN_KEYFRAMES
        {5105, "Open auto save"}, // GLOBAL_OPEN_AUTO_SAVE
        {5038, "Run the last loaded script"}, // GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT
        {5200, "Sort all lines by start time"}, // GLOBAL_SORT_ALL_BY_START_TIMES
        {5201, "Sort all lines by end time"}, // GLOBAL_SORT_ALL_BY_END_TIMES
        {5202, "Sort all lines by styles"}, // GLOBAL_SORT_ALL_BY_STYLE
        {5203, "Sort all lines by actor"}, // GLOBAL_SORT_ALL_BY_ACTOR
        {5204, "Sort all lines by effect"}, // GLOBAL_SORT_ALL_BY_EFFECT
        {5205, "Sort all lines by layer"}, // GLOBAL_SORT_ALL_BY_LAYER
        {5206, "Sort selected lines by start time"}, // GLOBAL_SORT_SELECTED_BY_START_TIMES
        {5207, "Sort selected lines by end time"}, // GLOBAL_SORT_SELECTED_BY_END_TIMES
        {5208, "Sort selected lines by styles"}, // GLOBAL_SORT_SELECTED_BY_STYLE
        {5209, "Sort selected lines by actor"}, // GLOBAL_SORT_SELECTED_BY_ACTOR
        {5210, "Sort selected lines by effect"}, // GLOBAL_SORT_SELECTED_BY_EFFECT
        {5211, "Sort selected lines by layer"}, // GLOBAL_SORT_SELECTED_BY_LAYER
        {5300, "Shift times / run time post processor"}, // GLOBAL_SHIFT_TIMES
        {5050, "Go to previous keyframe"}, // GLOBAL_GO_TO_PREVIOUS_KEYFRAME
        {5049, "Go to next keyframe"}, // GLOBAL_GO_TO_NEXT_KEYFRAME
        {5027, "Resample subtitles"}, // GLOBAL_OPEN_SUBS_RESAMPLE
        {5006, "Undo"}, // GLOBAL_UNDO
        {5007, "Undo to last save"}, // GLOBAL_UNDO_TO_LAST_SAVE
        {5015, "Open video with FFMS2"}, // GLOBAL_VIDEO_INDEXING
        {5043, "Zoom video"}, // GLOBAL_VIDEO_ZOOM
        {5044, "Turn off video zoom"}, // GLOBAL_RESET_VIDEO_ZOOM
        {5031, "View all"}, // GLOBAL_VIEW_ALL
        {5032, "View audio and subtitles"}, // GLOBAL_VIEW_AUDIO
        {5035, "View only subtitles"}, // GLOBAL_VIEW_SUBS
        {5033, "View video and subtitles"}, // GLOBAL_VIEW_VIDEO
        {5034, "View only video"}, // GLOBAL_VIEW_ONLY_VIDEO
        {5301, "Open new tab"}, // GLOBAL_ADD_PAGE
        {5302, "Close current tab"}, // GLOBAL_CLOSE_PAGE
        {5168, "Clean styles of ASS file"}, // GLOBAL_STYLE_MANAGER_CLEAN_STYLE
        {5012, "Fix minor errors (experimental)"}, // GLOBAL_MISSPELLS_REPLACER
        {5011, "Find next"}, // GLOBAL_FIND_NEXT
        {5055, "Load last session"}, // GLOBAL_LOAD_LAST_SESSION
        {5042, "Next frame"}, // GLOBAL_NEXT_FRAME
        {5151, "Next line"}, // GLOBAL_NEXT_LINE
        {5154, "Next tab"}, // GLOBAL_NEXT_TAB
        {5017, "Open audio"}, // GLOBAL_OPEN_AUDIO
        {5100, "Open subtitles"}, // GLOBAL_OPEN_SUBS
        {5101, "Open video"}, // GLOBAL_OPEN_VIDEO
        {5040, "Play / Pause"}, // GLOBAL_PLAY_PAUSE
        {5167, "Play active line"}, // GLOBAL_PLAY_ACTUAL_LINE
        {5041, "Previous frame"}, // GLOBAL_PREVIOUS_FRAME
        {5150, "Previous line"}, // GLOBAL_PREVIOUS_LINE
        {5155, "Previous tab"}, // GLOBAL_PREVIOUS_TAB
        {5005, "Redo"}, // GLOBAL_REDO
        {5156, "Delete line"}, // GLOBAL_REMOVE_LINES
        {5004, "Remove subtitles from the editor"}, // GLOBAL_REMOVE_SUBS
        {5157, "Delete text"}, // GLOBAL_REMOVE_TEXT
        {5001, "Save all subtitles"}, // GLOBAL_SAVE_ALL_SUBS
        {5000, "Save"}, // GLOBAL_SAVE_SUBS
        {5002, "Save as..."}, // GLOBAL_SAVE_SUBS_AS
        {5003, "Save translation"}, // GLOBAL_SAVE_TRANSLATION
        {5016, "Save subtitles using the video name"}, // GLOBAL_SAVE_WITH_VIDEO_NAME
        {5009, "Find"}, // GLOBAL_SEARCH
        {5166, "Select line at current video position"}, // GLOBAL_SELECT_FROM_VIDEO
        {5013, "Select lines"}, // GLOBAL_OPEN_SELECT_LINES
        {5051, "Set audio position to video time"}, // GLOBAL_SET_AUDIO_FROM_VIDEO
        {5052, "Set audio marker to video time"}, // GLOBAL_SET_AUDIO_MARK_FROM_VIDEO
        {5046, "Insert end time from video"}, // GLOBAL_SET_END_TIME
        {5045, "Insert start time from video"}, // GLOBAL_SET_START_TIME
        {5107, "Settings"}, // GLOBAL_SETTINGS
        {5047, "Go to start time"}, // GLOBAL_SET_VIDEO_AT_START_TIME
        {5048, "Go to end time of line"}, // GLOBAL_SET_VIDEO_AT_END_TIME
        {5159, "Change end time to nearest keyframe"}, // GLOBAL_SNAP_WITH_END
        {5158, "Change start time to nearest keyframe"}, // GLOBAL_SNAP_WITH_START
        {5014, "Check spelling"}, // GLOBAL_OPEN_SPELLCHECKER
        {5026, "Style manager"}, // GLOBAL_OPEN_STYLE_MANAGER
        {2018, "Copy frame to clipboard"}, // VIDEO_COPY_FRAME_TO_CLIPBOARD
        {2014, "Remove video"}, // VIDEO_DELETE_FILE
        {2017, "Save frame as PNG"}, // VIDEO_SAVE_FRAME_TO_PNG
        {2003, "5 seconds backward"}, // VIDEO_5_SECONDS_BACKWARD
        {2004, "1 minute backward"}, // VIDEO_MINUTE_BACKWARD
        {2013, "Show / hide progress bar"}, // VIDEO_HIDE_PROGRESS_BAR
        {2011, "Next chapter"}, // VIDEO_NEXT_CHAPTER
        {2009, "Next file"}, // VIDEO_NEXT_FILE
        {2000, "Play / Pause"}, // VIDEO_PLAY_PAUSE
        {2015, "Change aspect ratio"}, // VIDEO_ASPECT_RATIO
        {2002, "5 seconds forward"}, // VIDEO_5_SECONDS_FORWARD
        {2005, "1 minute forward"}, // VIDEO_MINUTE_FORWARD
        {2010, "Previous chapter"}, // VIDEO_PREVIOUS_CHAPTER
        {2008, "Previous file"}, // VIDEO_PREVIOUS_FILE
        {2020, "Copy frame with subtitles to clipboard"}, // VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD
        {2012, "Full screen"}, // VIDEO_FULL_SCREEN
        {2006, "Volume up"}, // VIDEO_VOLUME_PLUS
        {2007, "Volume down"}, // VIDEO_VOLUME_MINUS
        {2019, "Save frame with subtitles as PNG"}, // VIDEO_SAVE_SUBBED_FRAME_TO_PNG
        {2001, "Stop"}, // VIDEO_STOP
};

struct DefaultRow {
    int id;
    int type;
    const char *name;
    const char *accel;
};
// Hotkeys::LoadDefault, the main file's and the audio file's.
constexpr DefaultRow kMainDefaults[] = {
        {5108, GlobalHotkey, "Exit", "Alt-F4"}, // GLOBAL_QUIT
        {5301, GlobalHotkey, "Open new tab", "Ctrl-T"}, // GLOBAL_ADD_PAGE
        {5302, GlobalHotkey, "Close current tab", "Ctrl-W"}, // GLOBAL_CLOSE_PAGE
        {5030, GlobalHotkey, "Time shift window", "Ctrl-I"}, // GLOBAL_SHOW_SHIFT_TIMES
        {5020, GlobalHotkey, "Convert to ASS", "F9"}, // GLOBAL_CONVERT_TO_ASS
        {5021, GlobalHotkey, "Convert to SRT", "F8"}, // GLOBAL_CONVERT_TO_SRT
        {5023, GlobalHotkey, "Convert to MDVD", "F10"}, // GLOBAL_CONVERT_TO_MDVD
        {5024, GlobalHotkey, "Convert to MPL2", "F11"}, // GLOBAL_CONVERT_TO_MPL2
        {5022, GlobalHotkey, "Convert to TMP", "Ctrl-F12"}, // GLOBAL_CONVERT_TO_TMP
        {5026, GlobalHotkey, "Style manager", "Ctrl-M"}, // GLOBAL_OPEN_STYLE_MANAGER
        {5109, GlobalHotkey, "Enable / Disable editor", "Ctrl-E"}, // GLOBAL_EDITOR
        {5101, GlobalHotkey, "Open video", "Ctrl-Shift-O"}, // GLOBAL_OPEN_VIDEO
        {5009, GlobalHotkey, "Find", "Ctrl-F"}, // GLOBAL_SEARCH
        {5010, GlobalHotkey, "Find and replace", "Ctrl-H"}, // GLOBAL_FIND_REPLACE
        {5006, GlobalHotkey, "Undo", "Ctrl-Z"}, // GLOBAL_UNDO
        {5005, GlobalHotkey, "Redo", "Ctrl-Y"}, // GLOBAL_REDO
        {5008, GlobalHotkey, "History", "Ctrl-Shift-H"}, // GLOBAL_HISTORY
        {5100, GlobalHotkey, "Open subtitles", "Ctrl-O"}, // GLOBAL_OPEN_SUBS
        {5000, GlobalHotkey, "Save", "Ctrl-S"}, // GLOBAL_SAVE_SUBS
        {5002, GlobalHotkey, "Save as...", "Ctrl-Shift-S"}, // GLOBAL_SAVE_SUBS_AS
        {5157, GlobalHotkey, "Delete text", "Alt-Delete"}, // GLOBAL_REMOVE_TEXT
        {5156, GlobalHotkey, "Delete line", "Shift-Delete"}, // GLOBAL_REMOVE_LINES
        {5045, GlobalHotkey, "Insert start time from video", "Ctrl-Left"}, // GLOBAL_SET_START_TIME
        {5046, GlobalHotkey, "Insert end time from video", "Ctrl-Right"}, // GLOBAL_SET_END_TIME
        {5040, GlobalHotkey, "Play / Pause", "Alt-Space"}, // GLOBAL_PLAY_PAUSE
        {5041, GlobalHotkey, "Previous frame", "Left"}, // GLOBAL_PREVIOUS_FRAME
        {5042, GlobalHotkey, "Next frame", "Right"}, // GLOBAL_NEXT_FRAME
        {5150, GlobalHotkey, "Previous line", "Ctrl-Up"}, // GLOBAL_PREVIOUS_LINE
        {5151, GlobalHotkey, "Next line", "Ctrl-Down"}, // GLOBAL_NEXT_LINE
        {5011, GlobalHotkey, "Find next", "F3"}, // GLOBAL_FIND_NEXT
        {5152, GlobalHotkey, "Merge with previous line", "F4"}, // GLOBAL_JOIN_WITH_PREVIOUS
        {5153, GlobalHotkey, "Merge with next line", "F5"}, // GLOBAL_JOIN_WITH_NEXT
        {5158, GlobalHotkey, "Change start time to nearest keyframe", "Shift-Left"}, // GLOBAL_SNAP_WITH_START
        {5159, GlobalHotkey, "Change end time to nearest keyframe", "Shift-Right"}, // GLOBAL_SNAP_WITH_END
        {5154, GlobalHotkey, "Next tab", "Ctrl-PgDn"}, // GLOBAL_NEXT_TAB
        {5155, GlobalHotkey, "Previous tab", "Ctrl-PgUp"}, // GLOBAL_PREVIOUS_TAB
        {5166, GlobalHotkey, "Select line at current video position", "F2"}, // GLOBAL_SELECT_FROM_VIDEO
        {5112, GlobalHotkey, "HikariSub website", "F1"}, // GLOBAL_HELP
        {4527, GridHotkey, "Duplicate lines", "Ctrl-D"}, // GRID_DUPLICATE_LINES
        {4554, GridHotkey, "Copy columns", "Ctrl-Shift-C"}, // GRID_COPY_COLUMNS
        {4553, GridHotkey, "Paste columns", "Ctrl-Shift-V"}, // GRID_PASTE_COLUMNS
        {4534, GridHotkey, "Show subtitles preview", "Ctrl-Q"}, // GRID_SHOW_PREVIEW
        {2000, VideoHotkey, "Play / Pause", "Space"}, // VIDEO_PLAY_PAUSE
        {2002, VideoHotkey, "5 seconds forward", "L"}, // VIDEO_5_SECONDS_FORWARD
        {2003, VideoHotkey, "5 seconds backward", ";"}, // VIDEO_5_SECONDS_BACKWARD
        {2005, VideoHotkey, "1 minute forward", "Up"}, // VIDEO_MINUTE_FORWARD
        {2004, VideoHotkey, "1 minute backward", "Down"}, // VIDEO_MINUTE_BACKWARD
        {2009, VideoHotkey, "Next file", "."}, // VIDEO_NEXT_FILE
        {2008, VideoHotkey, "Previous file", ","}, // VIDEO_PREVIOUS_FILE
        {2006, VideoHotkey, "Volume up", "Num ."}, // VIDEO_VOLUME_PLUS
        {2007, VideoHotkey, "Volume down", "Num 0"}, // VIDEO_VOLUME_MINUS
        {2011, VideoHotkey, "Next chapter", "M"}, // VIDEO_NEXT_CHAPTER
        {2010, VideoHotkey, "Previous chapter", "N"}, // VIDEO_PREVIOUS_CHAPTER
        {3012, EditorHotkey, "Add bold", "Ctrl-B"}, // EDITBOX_INSERT_BOLD
        {3013, EditorHotkey, "Add italic", "Ctrl-I"}, // EDITBOX_INSERT_ITALIC
        {3014, EditorHotkey, "Add line wrap", "Shift-Enter"}, // EDITBOX_SPLIT_LINE
        {3015, EditorHotkey, "Insert difference from the start", "Ctrl-,"}, // EDITBOX_START_DIFFERENCE
        {3016, EditorHotkey, "Insert difference to the end", "Ctrl-."}, // EDITBOX_END_DIFFERENCE
        {3017, EditorHotkey, "Next unconfirmed line", "Ctrl-D"}, // EDITBOX_FIND_NEXT_DOUBTFUL
        {3018, EditorHotkey, "Next untranslated line", "Ctrl-R"}, // EDITBOX_FIND_NEXT_UNTRANSLATED
        {3019, EditorHotkey, "Mark as unconfirmed and go to the next line", "Alt-Down"}, // EDITBOX_SET_DOUBTFUL
        {3010, EditorHotkey, "Apply changes", "Ctrl-Enter"}, // EDITBOX_COMMIT
        {3011, EditorHotkey, "Apply the changes and go to the next line", "Enter"}, // EDITBOX_COMMIT_GO_NEXT_LINE
};
constexpr DefaultRow kAudioDefaults[] = {
        {1010, AudioHotkey, "Commit", "Enter"}, // AUDIO_COMMIT
        {1000, AudioHotkey, "Commit alt", "G"}, // AUDIO_COMMIT_ALT
        {1013, AudioHotkey, "Previous line", "Left"}, // AUDIO_PREVIOUS
        {1003, AudioHotkey, "Previous line alt", "Z"}, // AUDIO_PREVIOUS_ALT
        {1014, AudioHotkey, "Next line", "Right"}, // AUDIO_NEXT
        {1004, AudioHotkey, "Next line alt", "X"}, // AUDIO_NEXT_ALT
        {1011, AudioHotkey, "Play", "Down"}, // AUDIO_PLAY
        {1001, AudioHotkey, "Play alt", "S"}, // AUDIO_PLAY_ALT
        {1012, AudioHotkey, "Play line", "Up"}, // AUDIO_PLAY_LINE
        {1002, AudioHotkey, "Play line alt", "R"}, // AUDIO_PLAY_LINE_ALT
        {1015, AudioHotkey, "Stop", "H"}, // AUDIO_STOP
        {1025, AudioHotkey, "Go to selection", "B"}, // AUDIO_GOTO
        {1024, AudioHotkey, "Scroll left", "A"}, // AUDIO_SCROLL_RIGHT
        {1023, AudioHotkey, "Scroll right", "F"}, // AUDIO_SCROLL_LEFT
        {1016, AudioHotkey, "Play before the marker", "Num 0"}, // AUDIO_PLAY_BEFORE_MARK
        {1017, AudioHotkey, "Play after the marker", "Num ."}, // AUDIO_PLAY_AFTER_MARK
        {1020, AudioHotkey, "Play first 500ms", "E"}, // AUDIO_PLAY_500MS_FIRST
        {1021, AudioHotkey, "Play last 500ms", "D"}, // AUDIO_PLAY_500MS_LAST
        {1018, AudioHotkey, "Play 500ms before", "Q"}, // AUDIO_PLAY_500MS_BEFORE
        {1019, AudioHotkey, "Play 500ms after", "W"}, // AUDIO_PLAY_500MS_AFTER
        {1022, AudioHotkey, "Play to the end", "T"}, // AUDIO_PLAY_TO_END
        {1026, AudioHotkey, "Add lead-in", "C"}, // AUDIO_LEAD_IN
        {1027, AudioHotkey, "Add lead-out", "V"}, // AUDIO_LEAD_OUT
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

// Hotkeys::GetModifier: wxACCEL_ALT 1, wxACCEL_CTRL 2, wxACCEL_SHIFT 4,
// found anywhere in the text.
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

// GetHKey's key: '-' when the text ends with one, else what follows the last '-'.
std::string keyTextOf(std::string_view accel)
{
    if (accel.ends_with('-'))
        return "-";
    const auto dash = accel.rfind('-');
    return std::string(dash == std::string_view::npos ? accel : accel.substr(dash + 1));
}

// The UTF-16 length and the code point of `text` (UTF-8): wxString counts
// UTF-16 units on Windows.
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

// GetHKey's key code (Hotkeys.cpp:362-375): FillTable's by name, else the
// one character (akey.length() < 2, UTF-16 units); 0 is invalid, and legacy
// logs it ('Shortcut "%s" is invalid').
int keyCodeOf(const std::string &keyText)
{
    for (const auto &[code, name] : hotkeyKeyNames())
        if (name == keyText)
            return code;
    const auto [units, first] = utf16LengthAndFirst(keyText);
    if (units < 2)
        return static_cast<int>(first); // "" reads akey[0], the terminating 0
    return 0;
}

// FillTable names as Qt portable key names.
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

// The name a conflict is reported by: the names table's, else the binding's.
std::string reportName(const HotkeyId &key, const Hotkey &hotkey)
{
    auto name = hotkeyName(key.id);
    return name.empty() ? hotkey.name : name;
}

struct ConflictScan {
    bool doubled = false;
    std::string names;
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
            continue; // the automation hotkeys' (S2)
        // Legacy indexes "GSEVA" with the window even when it is out of range
        // (a window letter it could not read); the rewrite's lists never hold one.
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
    // _wtoi: leading blanks, a sign, then digits; out of range INT_MAX / INT_MIN.
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
        // wxString("GSEVA").find(Values[0]): -1 when not a window letter.
        const auto letter = values.empty() ? std::string::npos : kWindowLetters.find(values[0]);
        const int type = letter == std::string::npos ? -1 : int(letter);
        values = values.size() > 2 ? values.substr(2) : std::string();
        if (values.empty())
            continue;
        // Labels.IsNumber() (wxString::IsNumber: an optional sign, then
        // digits; "" counts too) and wxAtoi (Hotkeys.cpp:300-301), which
        // never fails: past the int range each legacy build reads it its
        // own way (R5-per-platform, hotkeyLabelNumber).
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
    // Hotkeys::FillTable.
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
    // Hotkeys.cpp:372-375: logged when the key is no FillTable name and
    // longer than one character.
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
        if (plus == start) { // the '+' key itself
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
                meta += p + "-"; // no legacy modifier ("Meta")
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
    // DialogueTextEditor's accelerator table: (modifiers, key).
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

// ---- The Options page (OptionsDialog.cpp, HikariListCtrl.cpp) ----

void HotkeyList::clear()
{
    // ClearList: the history goes, the list's modified flag stays. Legacy
    // also keeps the history position, so an edit or undo after "Set
    // default" reads and erases past the history's end (proposed departure
    // O2-reset-history: the position starts again with the history).
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
    // AddHotkeysOnList: the names table from the highest id down, so Global
    // first; when the window changes, the previous window's bindings that
    // were not listed by name (scripts, bindings copied to that window)
    // follow, except GLOBAL_QUIT. Once every binding is listed the rest of
    // the names are left out.
    HotkeyMap mapped = live;
    const auto &names = hotkeyNames();
    int lastType = -1;
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
    // StartEdition, SetSelection(0).
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

void HotkeyList::filter(int mode)
{
    // FilterList(1, mode) with ItemHotkey::OnVisibilityChange.
    m_filterMode = mode;
    m_sel = 0;
    m_isFiltered = false;
    for (auto &r : m_rows) {
        bool visible = true;
        switch (mode) {
        case 1: visible = !r->accel.empty(); break;
        case 2: visible = r->key.type == GlobalHotkey; break;
        case 3: visible = r->key.type == GridHotkey; break;
        case 4: visible = r->key.type == EditorHotkey; break;
        case 5: visible = r->key.type == VideoHotkey; break;
        case 6: visible = r->key.type == AudioHotkey; break;
        default: break;
        }
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
    // HikariListCtrl::CheckIfHasHiddenBlock (HikariListCtrl.cpp:968-986).
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
    // HikariListCtrl::OnMouseEvent's gutter click: ShowOrHideBlock(FindKey)
    // when CheckIfHasHiddenBlock finds a block (HikariListCtrl.cpp:712-724, 988-1001).
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
    // FindKey(sel).
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
    // FindId(key).
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
    row->visible = 1; // a new ItemRow
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

// The question both mapping paths ask (OnMapHotkey, Hotkeys::OnMapHkey).
HotkeyConflict question(const ConflictScan &scan, std::size_t count)
{
    HotkeyConflict c;
    c.doubled = scan.doubled;
    c.count = count;
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
}

} // namespace

std::optional<HotkeyConflict> HotkeyList::mapConflict(int index, const std::string &accel, int type,
                                                      const HotkeyMap &live)
{
    ensureCopy(live);
    const auto found = conflicts(index, accel);
    if (found.empty())
        return std::nullopt;
    // A binding in the same window, both in Video/Audio, or a video play/seek
    // id against a Subtitles..Audio window is "the same" here.
    const int id = row(index).key.id;
    ConflictScan scan;
    for (const auto &k : found) {
        if (k.type == type || (k.type >= VideoHotkey && type >= VideoHotkey) ||
            ((inVideoPlayRange(id) || inVideoPlayRange(k.id)) && (k.type >= GridHotkey || type >= GridHotkey))) {
            scan.doubled = true;
            scan.names = reportName(k, m_copy.at(k));
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
            // Only rows found by "<window> <names table name>" change, and
            // with the copy only through them.
            const std::string hotkey = answer == HotkeyAnswer::Delete ? std::string() : row(index).accel;
            for (const auto &k : conflicts(index, accel)) {
                if (c->doubled && k.type != type)
                    continue;
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
        // Another window: its row for this action, or a new row at the end.
        const int other = find(std::string(hotkeyWindowName(type)) + " " + current.name);
        if (other < 0) {
            auto &r = copyRow(index, true);
            r.text = std::string(hotkeyWindowName(type)) + " " + current.name;
            r.textModified = true;
            r.accel = accel;
            r.keyModified = true;
            r.key = HotkeyId{current.key.id, type};
            m_sel = findShown(int(m_rows.size())); // SetSelection(GetCount()): none
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
        return;
    }
    auto &r = copyRow(index);
    r.accel = accel;
    r.keyModified = true;
    r.textModified = true;
    m_copy[current.key] = Hotkey{current.name, accel};
    m_modified = true;
    pushHistory();
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
}

void HotkeyList::moveInHistory(int to)
{
    // Undo/Redo: the rows that differ from the snapshot write the snapshot's
    // binding into the copy (ItemHotkey::OnChangeHistory); rows past the
    // snapshot's end are dropped from the list but not from the copy.
    m_iter = to;
    std::vector<RowPtr> actual = m_history[std::size_t(m_iter)];
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (i < actual.size() && m_rows[i] != actual[i]) {
            m_copy[actual[i]->key] = Hotkey{actual[i]->name, actual[i]->accel};
            m_modified = true;
        }
    m_rows = std::move(actual);
    rebuildFiltered();
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
    // SaveAll(1): every cell's flag goes, in the rows the history shares too.
    for (auto &r : m_rows) {
        r->keyModified = false;
        r->textModified = false;
    }
    m_modified = false;
    return m_copy;
}

// ---- Hotkeys::OnMapHkey ----

namespace {

ConflictScan scanNow(const HotkeyMap &live, const std::vector<HotkeyId> &found, int type)
{
    ConflictScan scan;
    for (const auto &k : found) {
        if (k.type == type) {
            scan.doubled = true;
            scan.names = reportName(k, live.at(k));
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
                    // Hotkeys.cpp:497-509, in map order: the binding is
                    // cleared, then given (id, window)'s keys as they are
                    // now. When (id, window) is among them it clears itself
                    // there, so the bindings after it get nothing; SetHKey
                    // gives it the new keys afterwards.
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
