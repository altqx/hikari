#include "hikari/application/settings_import.h"

#include "hikari/json/json.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace hikari::application::settings_import {

namespace {

constexpr std::string_view kNone = "HikariSub has no such command";
constexpr std::string_view kMenu = "a menu of recent files, not a command";
constexpr std::string_view kMac = "a macOS window command; the system's shortcut does it";
constexpr std::string_view kTranslation = "HikariSub has no Translation Assistant";
constexpr std::string_view kStyling = "HikariSub has no Styling Assistant";
constexpr std::string_view kViews = "retired: layout presets and saved layouts replace the View arrangements";
constexpr std::string_view kAudioOption = "an audio display switch, set in Settings › Audio here";
constexpr std::string_view kCommitSetting =
    "Apply changes goes to the next line or stays as Settings › Audio says, so neither variant has a command";

constexpr AegisubCommand kCommands[] = {
    {"am/manager", "", "HikariSub's macros are in the Automation panel, with no manager window"},
    {"am/meta", "", "HikariSub's macros are in the Automation panel, with no manager window"},
    {"am/reload", "", "HikariSub reloads only the autoload scripts: Reload autoload Automation scripts"},
    {"am/reload/autoload", "GLOBAL_AUTOMATION_RELOAD_AUTOLOAD", ""},
    {"app/about", "GLOBAL_ABOUT", ""},
    {"app/bring_to_front", "", kMac},
    {"app/display/audio_subs", "", kViews},
    {"app/display/full", "", kViews},
    {"app/display/subs", "", kViews},
    {"app/display/video_subs", "", kViews},
    {"app/exit", "GLOBAL_QUIT", ""},
    {"app/hide", "", kMac},
    {"app/hide_others", "", kMac},
    {"app/language", "", "the language is set in Settings › Appearance"},
    {"app/log", "", kNone},
    {"app/maximize", "", kMac},
    {"app/minimize", "", kMac},
    {"app/new_window", "", "HikariSub opens a window by moving a tab to it"},
    {"app/options", "GLOBAL_SETTINGS", ""},
    {"app/show_all", "", kMac},
    {"app/toggle/global_hotkeys", "", "set in Settings › Audio › Timing keys work in every panel"},
    {"app/toggle/toolbar", "", "HikariSub has no main toolbar"},
    {"app/updates", "GLOBAL_CHECK_FOR_UPDATES", ""},
    {"audio/close", "GLOBAL_CLOSE_AUDIO", ""},
    {"audio/commit", "AUDIO_COMMIT", ""},
    {"audio/commit/default", "AUDIO_COMMIT_DEFAULT", ""},
    {"audio/commit/next", "", kCommitSetting},
    {"audio/commit/stay", "", kCommitSetting},
    {"audio/go_to", "AUDIO_GOTO", ""},
    {"audio/go_to/end", "AUDIO_GOTO_END", ""},
    {"audio/go_to/start", "AUDIO_GOTO_START", ""},
    {"audio/karaoke", "", "karaoke mode is the audio panel's switch"},
    {"audio/open", "GLOBAL_OPEN_AUDIO", ""},
    {"audio/open/blank", "", "retired: a dummy video brings its own silent audio"},
    {"audio/open/noise", "", "retired: a dummy video brings its own silent audio"},
    {"audio/open/video", "GLOBAL_AUDIO_FROM_VIDEO", ""},
    {"audio/opt/autocommit", "", kAudioOption},
    {"audio/opt/autonext", "", kAudioOption},
    {"audio/opt/autoscroll", "", kAudioOption},
    {"audio/opt/spectrum", "", kAudioOption},
    {"audio/opt/vertical_link", "", kAudioOption},
    {"audio/play/current", "AUDIO_PLAY", ""},
    {"audio/play/line", "AUDIO_PLAY_LINE", ""},
    {"audio/play/selection", "AUDIO_PLAY", ""},
    {"audio/play/selection/after", "AUDIO_PLAY_500MS_AFTER", ""},
    {"audio/play/selection/before", "AUDIO_PLAY_500MS_BEFORE", ""},
    {"audio/play/selection/begin", "AUDIO_PLAY_500MS_FIRST", ""},
    {"audio/play/selection/end", "AUDIO_PLAY_500MS_LAST", ""},
    {"audio/play/to_end", "AUDIO_PLAY_TO_END", ""},
    {"audio/play/toggle", "AUDIO_PLAY", ""},
    {"audio/save/clip", "GLOBAL_SAVE_SELECTED_AUDIO", ""},
    // MUST KILL: AUDIO_SCROLL_RIGHT runs scrollLeft
    {"audio/scroll/left", "AUDIO_SCROLL_RIGHT", ""},
    {"audio/scroll/right", "AUDIO_SCROLL_LEFT", ""},
    {"audio/stop", "AUDIO_STOP", ""},
    {"audio/view/spectrum", "", kAudioOption},
    {"audio/view/waveform", "", kAudioOption},
    {"edit/clear", "", kNone},
    {"edit/clear/text", "GLOBAL_REMOVE_TEXT", ""},
    {"edit/color/outline", "EDITBOX_CHANGE_COLOR_OUTLINE", ""},
    {"edit/color/primary", "EDITBOX_CHANGE_COLOR_PRIMARY", ""},
    {"edit/color/secondary", "EDITBOX_CHANGE_COLOR_SECONDARY", ""},
    {"edit/color/shadow", "EDITBOX_CHANGE_COLOR_SHADOW", ""},
    {"edit/find_replace", "GLOBAL_FIND_REPLACE", ""},
    {"edit/font", "EDITBOX_CHANGE_FONT", ""},
    {"edit/insert_original", "", kTranslation},
    {"edit/line/copy", "GRID_COPY", ""},
    {"edit/line/cut", "GRID_CUT", ""},
    {"edit/line/delete", "GLOBAL_REMOVE_LINES", ""},
    {"edit/line/duplicate", "GRID_DUPLICATE_LINES", ""},
    {"edit/line/duplicate/shift", "", kNone},
    {"edit/line/duplicate/shift_back", "", kNone},
    {"edit/line/join/as_karaoke", "", kNone},
    {"edit/line/join/concatenate", "GRID_JOIN_LINES", ""},
    {"edit/line/join/keep_first", "GRID_JOIN_TO_FIRST_LINE", ""},
    {"edit/line/paste", "GRID_PASTE", ""},
    {"edit/line/paste/over", "GRID_PASTE_COLUMNS", ""},
    {"edit/line/recombine", "", kNone},
    {"edit/line/split/after", "", kNone},
    {"edit/line/split/before", "", kNone},
    {"edit/line/split/by_karaoke", "", kNone},
    {"edit/line/split/estimate", "", kNone},
    {"edit/line/split/preserve", "", kNone},
    {"edit/line/split/video", "", kNone},
    {"edit/redo", "GLOBAL_REDO", ""},
    {"edit/revert", "", kNone},
    {"edit/style/bold", "EDITBOX_INSERT_BOLD", ""},
    {"edit/style/italic", "EDITBOX_INSERT_ITALIC", ""},
    {"edit/style/strikeout", "EDITBOX_CHANGE_STRIKEOUT", ""},
    {"edit/style/underline", "EDITBOX_CHANGE_UNDERLINE", ""},
    {"edit/undo", "GLOBAL_UNDO", ""},
    {"grid/line/next", "GLOBAL_NEXT_LINE", ""},
    {"grid/line/next/create", "", kNone},
    {"grid/line/prev", "GLOBAL_PREVIOUS_LINE", ""},
    {"grid/move/down", "", kNone},
    {"grid/move/up", "", kNone},
    {"grid/sort/actor", "GLOBAL_SORT_ALL_BY_ACTOR", ""},
    {"grid/sort/actor/selected", "GLOBAL_SORT_SELECTED_BY_ACTOR", ""},
    {"grid/sort/effect", "GLOBAL_SORT_ALL_BY_EFFECT", ""},
    {"grid/sort/effect/selected", "GLOBAL_SORT_SELECTED_BY_EFFECT", ""},
    {"grid/sort/end", "GLOBAL_SORT_ALL_BY_END_TIMES", ""},
    {"grid/sort/end/selected", "GLOBAL_SORT_SELECTED_BY_END_TIMES", ""},
    {"grid/sort/layer", "GLOBAL_SORT_ALL_BY_LAYER", ""},
    {"grid/sort/layer/selected", "GLOBAL_SORT_SELECTED_BY_LAYER", ""},
    {"grid/sort/start", "GLOBAL_SORT_ALL_BY_START_TIMES", ""},
    {"grid/sort/start/selected", "GLOBAL_SORT_SELECTED_BY_START_TIMES", ""},
    {"grid/sort/style", "GLOBAL_SORT_ALL_BY_STYLE", ""},
    {"grid/sort/style/selected", "GLOBAL_SORT_SELECTED_BY_STYLE", ""},
    {"grid/swap", "GRID_SWAP_LINES", ""},
    {"grid/tag/cycle_hiding", "GLOBAL_HIDE_TAGS", ""},
    {"grid/tags/hide", "", kNone},
    {"grid/tags/show", "", kNone},
    {"grid/tags/simplify", "", kNone},
    {"help/bugs", "GLOBAL_ANSI", ""},
    {"help/contents", "GLOBAL_HELP", ""},
    {"help/irc", "", kNone},
    {"help/video", "", kNone},
    {"help/website", "", kNone},
    {"keyframe/close", "GLOBAL_CLOSE_KEYFRAMES", ""},
    {"keyframe/open", "GLOBAL_OPEN_KEYFRAMES", ""},
    {"keyframe/save", "GLOBAL_SAVE_KEYFRAMES", ""},
    {"recent/audio/", "", kMenu},
    {"recent/keyframes/", "", kMenu},
    {"recent/subtitle/", "", kMenu},
    {"recent/timecodes/", "", kMenu},
    {"recent/video/", "", kMenu},
    {"subtitle/attachment", "", kNone},
    {"subtitle/close", "GLOBAL_CLOSE_PAGE", ""},
    {"subtitle/find", "GLOBAL_SEARCH", ""},
    {"subtitle/find/next", "GLOBAL_FIND_NEXT", ""},
    {"subtitle/insert/after", "GRID_INSERT_AFTER", ""},
    {"subtitle/insert/after/videotime", "GRID_INSERT_AFTER_VIDEO", ""},
    {"subtitle/insert/before", "GRID_INSERT_BEFORE", ""},
    {"subtitle/insert/before/videotime", "GRID_INSERT_BEFORE_VIDEO", ""},
    {"subtitle/new", "GLOBAL_ADD_PAGE", ""},
    {"subtitle/open", "GLOBAL_OPEN_SUBS", ""},
    {"subtitle/open/autosave", "GLOBAL_OPEN_AUTO_SAVE", ""},
    {"subtitle/open/charset", "", "HikariSub detects the encoding; files that are not UTF-8 are asked about"},
    {"subtitle/open/video", "GRID_SUBS_FROM_MKV", ""},
    {"subtitle/properties", "GLOBAL_OPEN_ASS_PROPERTIES", ""},
    {"subtitle/save", "GLOBAL_SAVE_SUBS", ""},
    {"subtitle/save/as", "GLOBAL_SAVE_SUBS_AS", ""},
    {"subtitle/select/all", "", "Select all is the grid's own Ctrl+A"},
    {"subtitle/select/visible", "GRID_SELECT_VISIBLE_LINES", ""},
    {"subtitle/spellcheck", "GLOBAL_OPEN_SPELLCHECKER", ""},
    {"time/continuous/end", "GRID_MAKE_CONTINOUS_NEXT_LINE", ""},
    {"time/continuous/start", "GRID_MAKE_CONTINOUS_PREVIOUS_LINE", ""},
    {"time/frame/current", "GLOBAL_SHIFT_FRAMES_TO_VIDEO_START", ""},
    {"time/lead/both", "", kNone},
    {"time/lead/in", "AUDIO_LEAD_IN", ""},
    {"time/lead/out", "AUDIO_LEAD_OUT", ""},
    {"time/length/decrease", "GLOBAL_END_FRAME_EARLIER", ""},
    {"time/length/decrease/shift", "GLOBAL_END_EARLIER_SHIFT_FOLLOWING", ""},
    {"time/length/increase", "GLOBAL_END_FRAME_LATER", ""},
    {"time/length/increase/shift", "GLOBAL_END_LATER_SHIFT_FOLLOWING", ""},
    {"time/next", "AUDIO_NEXT", ""},
    {"time/prev", "AUDIO_PREVIOUS", ""},
    {"time/shift", "GLOBAL_SHOW_SHIFT_TIMES", ""},
    {"time/snap/end_video", "GLOBAL_SET_END_TIME", ""},
    {"time/snap/scene", "GLOBAL_SNAP_TO_SCENE", ""},
    {"time/snap/start_video", "GLOBAL_SET_START_TIME", ""},
    {"time/start/decrease", "GLOBAL_START_FRAME_EARLIER", ""},
    {"time/start/increase", "GLOBAL_START_FRAME_LATER", ""},
    {"timecode/close", "GLOBAL_CLOSE_TIMECODES", ""},
    {"timecode/open", "GLOBAL_OPEN_TIMECODES", ""},
    {"timecode/save", "GLOBAL_SAVE_TIMECODES", ""},
    {"tool/export", "GLOBAL_EXPORT_SUBS_AS", ""},
    {"tool/font_collector", "GLOBAL_OPEN_FONT_COLLECTOR", ""},
    {"tool/line/select", "GLOBAL_OPEN_SELECT_LINES", ""},
    {"tool/resampleres", "GLOBAL_OPEN_SUBS_RESAMPLE", ""},
    {"tool/style/assistant", "", kStyling},
    {"tool/style/manager", "GLOBAL_OPEN_STYLE_MANAGER", ""},
    {"tool/styling_assistant/commit", "", kStyling},
    {"tool/styling_assistant/preview", "", kStyling},
    {"tool/time/kanji", "GLOBAL_KANJI_TIMER", ""},
    {"tool/time/postprocess", "GLOBAL_SHOW_TIMING_POSTPROCESSOR", ""},
    {"tool/translation_assistant", "", kTranslation},
    {"tool/translation_assistant/commit", "", kTranslation},
    {"tool/translation_assistant/insert_original", "", kTranslation},
    {"tool/translation_assistant/next", "", kTranslation},
    {"tool/translation_assistant/prev", "", kTranslation},
    {"tool/translation_assistant/preview", "", kTranslation},
    {"video/aspect/cinematic", "", "the aspect ratio is chosen in Change aspect ratio…"},
    {"video/aspect/custom", "VIDEO_ASPECT_RATIO", ""},
    {"video/aspect/default", "", "the aspect ratio is chosen in Change aspect ratio…"},
    {"video/aspect/full", "", "the aspect ratio is chosen in Change aspect ratio…"},
    {"video/aspect/wide", "", "the aspect ratio is chosen in Change aspect ratio…"},
    {"video/close", "VIDEO_DELETE_FILE", ""},
    {"video/copy_coordinates", "", kNone},
    {"video/detach", "", "HikariSub moves the video panel by its layout"},
    {"video/details", "GLOBAL_VIDEO_DETAILS", ""},
    {"video/focus_seek", "", kNone},
    {"video/frame/copy", "VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD", ""},
    {"video/frame/copy/raw", "VIDEO_COPY_FRAME_TO_CLIPBOARD", ""},
    {"video/frame/copy/subs", "VIDEO_COPY_SUBTITLES_TO_CLIPBOARD", ""},
    {"video/frame/next", "GLOBAL_NEXT_FRAME", ""},
    {"video/frame/next/boundary", "", kNone},
    {"video/frame/next/keyframe", "GLOBAL_GO_TO_NEXT_KEYFRAME", ""},
    {"video/frame/next/large", "", kNone},
    {"video/frame/prev", "GLOBAL_PREVIOUS_FRAME", ""},
    {"video/frame/prev/boundary", "", kNone},
    {"video/frame/prev/keyframe", "GLOBAL_GO_TO_PREVIOUS_KEYFRAME", ""},
    {"video/frame/prev/large", "", kNone},
    {"video/frame/save", "VIDEO_SAVE_SUBBED_FRAME_TO_PNG", ""},
    {"video/frame/save/raw", "VIDEO_SAVE_FRAME_TO_PNG", ""},
    {"video/frame/save/subs", "VIDEO_SAVE_SUBTITLES_TO_PNG", ""},
    {"video/jump", "", kNone},
    {"video/jump/end", "GLOBAL_SET_VIDEO_AT_END_TIME", ""},
    {"video/jump/start", "GLOBAL_SET_VIDEO_AT_START_TIME", ""},
    {"video/open", "GLOBAL_OPEN_VIDEO", ""},
    {"video/open/dummy", "GLOBAL_OPEN_DUMMY_VIDEO", ""},
    {"video/opt/autoscroll", "", kNone},
    {"video/play", "GLOBAL_PLAY_PAUSE", ""},
    {"video/play/line", "GLOBAL_PLAY_ACTUAL_LINE", ""},
    {"video/reset_pan", "GLOBAL_RESET_VIDEO_ZOOM", ""},
    {"video/show_overscan", "", kNone},
    {"video/stop", "VIDEO_STOP", ""},
    {"video/subtitles_provider/cycle", "", "the renderer is chosen in Settings › Video"},
    {"video/tool/clip", "VIDEO_TOOL_RECTANGLE_CLIP", ""},
    {"video/tool/cross", "VIDEO_TOOL_CROSSHAIR", ""},
    {"video/tool/drag", "VIDEO_TOOL_POSITION", ""},
    {"video/tool/rotate/xy", "VIDEO_TOOL_ROTATION_XY", ""},
    {"video/tool/rotate/z", "VIDEO_TOOL_ROTATION_Z", ""},
    {"video/tool/scale", "VIDEO_TOOL_SCALE", ""},
    {"video/tool/vclip/bicubic", "VIDEO_VECTOR_CLIP_BEZIER", ""},
    {"video/tool/vclip/convert", "", kNone},
    {"video/tool/vclip/drag", "VIDEO_VECTOR_CLIP_DRAG", ""},
    {"video/tool/vclip/freehand", "", kNone},
    {"video/tool/vclip/freehand_smooth", "", kNone},
    {"video/tool/vclip/insert", "", kNone},
    {"video/tool/vclip/line", "VIDEO_VECTOR_CLIP_LINE", ""},
    {"video/tool/vclip/remove", "VIDEO_VECTOR_CLIP_DELETE", ""},
    {"video/tool/vector_clip", "VIDEO_TOOL_VECTOR_CLIP", ""},
    {"video/zoom/100", "", kNone},
    {"video/zoom/200", "", kNone},
    {"video/zoom/50", "", kNone},
    {"video/zoom/in", "", kNone},
    {"video/zoom/out", "", kNone},
};

std::string keyOfAegisubName(std::string_view name)
{
    static constexpr std::pair<std::string_view, std::string_view> names[] = {
        {"Backspace", "Backspace"}, {"Tab", "Tab"},         {"Enter", "Enter"},        {"Return", "Enter"},
        {"Space", "Space"},         {"Delete", "Delete"},   {"Pause", "Pause"},        {"End", "End"},
        {"Home", "Home"},           {"Left", "Left"},       {"Up", "Up"},              {"Right", "Right"},
        {"Down", "Down"},           {"Insert", "Insert"},   {"PageUp", "PgUp"},        {"PageDown", "PgDn"},
        {"Asterisk", "*"},          {"Plus", "+"},          {"Hyphen", "-"},           {"Period", "."},
        {"Slash", "/"},             {"KP_Enter", "Num Enter"}, {"KP_Add", "Num +"},    {"KP_Subtract", "Num -"},
        {"KP_Multiply", "Num *"},   {"KP_Divide", "Num /"}, {"KP_Decimal", "Num ."},
    };
    for (const auto &[from, to] : names)
        if (from == name)
            return std::string(to);
    if (name.size() == 4 && name.starts_with("KP_") && name[3] >= '0' && name[3] <= '9')
        return "Num " + std::string(1, name[3]);
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F' &&
        std::ranges::all_of(name.substr(1), [](char c) { return c >= '0' && c <= '9'; })) {
        const int n = std::stoi(std::string(name.substr(1)));
        return n >= 1 && n <= 12 ? std::string(name) : std::string();
    }
    if (name.size() == 1 && name[0] > 0x20 && name[0] < 0x7f)
        return std::string(1, char(std::toupper(static_cast<unsigned char>(name[0]))));
    return {};
}

}

AegisubHotkeyFile readAegisubHotkeyFile(std::string_view text)
{
    AegisubHotkeyFile out;
    const auto parsed = json::Json::parse(text);
    if (!parsed) {
        out.error = "not JSON: " + parsed.error().message + " at byte " + std::to_string(parsed.error().offset);
        return out;
    }
    if (!parsed->isObject()) {
        out.error = "not a hotkey.json: the file is no JSON object";
        return out;
    }
    for (const auto &[context, commands] : parsed->asObject()) {
        if (!commands.isObject())
            continue;
        for (const auto &[command, keys] : commands.asObject()) {
            if (!keys.isArray())
                continue;
            for (const auto &key : keys.asArray()) {
                if (key.isString()) {
                    out.hotkeys.push_back({context, command, key.asString()});
                    continue;
                }
                const auto *mods = key.find("modifiers");
                const auto *name = key.find("key");
                if (!mods || !mods->isArray() || !name || !name->isString())
                    continue;
                std::string combo;
                for (const auto &m : mods->asArray())
                    if (m.isString())
                        combo += m.asString() + "-";
                out.hotkeys.push_back({context, command, combo + name->asString()});
                out.oldForm = true;
            }
        }
    }
    return out;
}

std::string accelOfAegisubKeys(std::string_view keys)
{
    bool alt = false, ctrl = false, shift = false;
    for (;;) {
        if (keys.starts_with("Ctrl-") && keys.size() > 5)
            ctrl = true, keys.remove_prefix(5);
        else if (keys.starts_with("Alt-") && keys.size() > 4)
            alt = true, keys.remove_prefix(4);
        else if (keys.starts_with("Shift-") && keys.size() > 6)
            shift = true, keys.remove_prefix(6);
        else
            break;
    }
    const std::string key = keyOfAegisubName(keys);
    if (key.empty())
        return {};
    return std::string(alt ? "Alt-" : "") + (ctrl ? "Ctrl-" : "") + (shift ? "Shift-" : "") + key;
}

std::span<const AegisubCommand> aegisubCommands()
{
    return kCommands;
}

int aegisubWindow(std::string_view context, int id)
{
    static constexpr std::string_view panels[] = {"Default", "Always", "Audio", "Video", "Subtitle Grid",
                                                  "Subtitle Edit Box"};
    if (std::ranges::find(panels, context) == std::end(panels))
        return -1;
    const int own = hotkeyType(id);
    if (own == AudioHotkey || context == "Always" || context == "Audio")
        return AudioHotkey;
    if (context == "Video")
        return VideoHotkey;
    if (context == "Subtitle Grid")
        return GridHotkey;
    if (context == "Subtitle Edit Box")
        return own == GridHotkey ? GridHotkey : EditorHotkey;
    return own;
}

}
