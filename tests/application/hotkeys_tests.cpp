// O2: hotkeys and the Options dialog's Hotkeys page against legacy
// Hotkeys.h/.cpp, HotkeysNaming.cpp, OptionsDialog.cpp (ItemHotkey,
// AddHotkeysOnList, SetOptions, ResetDefault) and HikariListCtrl.cpp at
// 20d647c4.

#include "hikari/application/hotkeys.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

using namespace hikari::application;

namespace {

std::string readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Legacy's IDS: the first kLegacyHotkeyActions rows of the action table.
const std::map<std::string, int> &ids()
{
    static const auto table = [] {
        std::map<std::string, int> out;
        for (const auto &a : hotkeyActions().first(kLegacyHotkeyActions))
            out[std::string(a.symbol)] = a.id;
        return out;
    }();
    return table;
}

HotkeyList::Row rowNamed(const HotkeyList &list, const std::string &text)
{
    const int i = list.find(text);
    EXPECT_GE(i, 0) << text;
    return i >= 0 ? list.row(i) : HotkeyList::Row{};
}

constexpr int kSave = 5000;        // GLOBAL_SAVE_SUBS, Ctrl-S
constexpr int kHistory = 5008;     // GLOBAL_HISTORY, Ctrl-Shift-H
constexpr int kDuplicate = 4527;   // GRID_DUPLICATE_LINES, Ctrl-D
constexpr int kNextDoubtful = 3017; // EDITBOX_FIND_NEXT_DOUBTFUL, Ctrl-D

// VT2: a Video command per visual tool, named as the rail names it, with
// the audit's single-key defaults (#270; VT3's Perspective, #273).
struct ToolCommand {
    int id;
    const char *symbol, *name, *accel;
};
constexpr ToolCommand kToolCommands[] = {
        {2022, "VIDEO_TOOL_CROSSHAIR", "Position pointer", "X"},
        {2023, "VIDEO_TOOL_POSITION", "Text positioning", "P"},
        {2024, "VIDEO_TOOL_MOVE", "Text moving", "G"},
        {2025, "VIDEO_TOOL_SCALE", "Text scaling", "S"},
        {2026, "VIDEO_TOOL_ROTATION_Z", "Text Z rotation", "R"},
        {2027, "VIDEO_TOOL_ROTATION_XY", "Text X / Y rotation", "T"},
        {2028, "VIDEO_TOOL_RECTANGLE_CLIP", "Rectangle clipping", "C"},
        {2029, "VIDEO_TOOL_VECTOR_CLIP", "Vector clipping", "V"},
        {2030, "VIDEO_TOOL_DRAWING", "Vector drawing", "D"},
        {2031, "VIDEO_TOOL_POSITION_SHIFTER", "Position shifter", "A"},
        {2032, "VIDEO_TOOL_HYDRA", "Hydra", "H"},
        {2033, "VIDEO_TOOL_PERSPECTIVE", "Perspective", "Q"}, // VT3 (#273)
};

} // namespace

TEST(Hotkeys, ActionTableIsLegacyIdsAndTheMigrationMap)
{
    ASSERT_EQ(ids().size(), 243u);
    // Legacy's IDS, then the rewrite's own commands (LE3: GLOBAL_EDITOR_HIDE_TAGS;
    // KE1: GLOBAL_VIDEO_RENDER_COST; FS1: VIDEO_FULLSCREEN_BAR and VT2's
    // eleven tool commands and VT3's Perspective, Video commands; TM1: GLOBAL_TRANSLATION_MODE, the
    // nine run keys and the next run; KA7: GLOBAL_CHECK_KARAOKE; KA2: AUDIO_TAP, AUDIO_SPEED_DOWN,
    // AUDIO_SPEED_UP and KA1: AUDIO_KARAOKE_SPLIT and AUDIO_KARAOKE_KF, Audio commands; AD2:
    // GLOBAL_AUTOMATION_REPEAT_LAST_RUN; KA3: GLOBAL_KANJI_TIMER; FR1: GLOBAL_GETTING_STARTED).
    ASSERT_EQ(hotkeyActions().size(), 278u);
    for (const auto &a : hotkeyActions().first(kLegacyHotkeyActions)) {
        EXPECT_EQ(hotkeyIdOf(a.symbol), a.id);
        EXPECT_EQ(hotkeySymbol(a.id), a.symbol);
    }
    for (const auto &a : hotkeyActions().subspan(kLegacyHotkeyActions)) {
        EXPECT_FALSE(ids().contains(std::string(a.symbol))) << a.symbol;
        EXPECT_TRUE(std::ranges::none_of(ids(), [&](const auto &l) { return l.second == a.id; })) << a.symbol;
        // The run keys work in the Translated field: Editor hotkeys; FS1's
        // bar mode and VT2's tools are Video ones.
        const bool editor = std::string_view(a.symbol).starts_with("EDITBOX_");
        const bool audio = a.id >= 1028 && a.id <= 1032; // KA1, KA2
        EXPECT_EQ(hotkeyType(a.id), editor                         ? EditorHotkey
                                    : a.id >= 2021 && a.id <= 2033 ? VideoHotkey
                                    : audio                        ? AudioHotkey
                                                                   : GlobalHotkey)
            << a.symbol;
        EXPECT_EQ(hotkeyIdOf(a.symbol), a.id);
        EXPECT_EQ(hotkeySymbol(a.id), a.symbol);
    }
    EXPECT_EQ(hotkeyIdOf("GLOBAL_EDITOR_HIDE_TAGS"), 5169);
    EXPECT_EQ(hotkeyIdOf("GLOBAL_VIDEO_RENDER_COST"), 5170);
    EXPECT_EQ(hotkeyIdOf("VIDEO_FULLSCREEN_BAR"), 2021);
    EXPECT_EQ(hotkeyIdOf("GLOBAL_TRANSLATION_MODE"), 5171);
    EXPECT_EQ(hotkeyIdOf("EDITBOX_PLACE_RUN_1"), 3020);
    EXPECT_EQ(hotkeyIdOf("EDITBOX_PLACE_RUN_9"), 3028);
    EXPECT_EQ(hotkeyIdOf("EDITBOX_PLACE_NEXT_RUN"), 3029);
    EXPECT_EQ(hotkeyIdOf("AUDIO_KARAOKE_SPLIT"), 1028);
    EXPECT_EQ(hotkeyIdOf("AUDIO_KARAOKE_KF"), 1029);
    EXPECT_EQ(hotkeyIdOf("AUDIO_TAP"), 1030);
    EXPECT_EQ(hotkeyIdOf("AUDIO_SPEED_DOWN"), 1031);
    EXPECT_EQ(hotkeyIdOf("AUDIO_SPEED_UP"), 1032);
    for (const auto &tool : kToolCommands)
        EXPECT_EQ(hotkeyIdOf(tool.symbol), tool.id) << tool.symbol;
    EXPECT_EQ(hotkeyIdOf("GLOBAL_CHECK_KARAOKE"), 5172);
    EXPECT_EQ(hotkeyIdOf("GLOBAL_AUTOMATION_REPEAT_LAST_RUN"), 5173);
    EXPECT_EQ(hotkeyIdOf("GLOBAL_KANJI_TIMER"), 5174);
    EXPECT_EQ(hotkeyIdOf("GLOBAL_GETTING_STARTED"), 5176);
    // docs/qt/proposals/settings-migration-map/actions.csv: symbol, numeric alias.
    std::istringstream csv(readFile(HIKARI_ACTIONS_CSV));
    std::string line;
    std::getline(csv, line);
    int rows = 0;
    const std::regex row(R"re(^"(\w+)","(\d+)")re");
    while (std::getline(csv, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, row))
            continue;
        ++rows;
        EXPECT_EQ(hotkeyIdOf(m[1].str()), std::stoi(m[2])) << m[1];
    }
    EXPECT_EQ(rows, 243);
    EXPECT_EQ(hotkeyIdOf("NOT_A_SYMBOL"), 0); // GetIdValue's input error
    EXPECT_EQ(hotkeySymbol(30100), "");
    EXPECT_EQ(kGridFilterIgnoreInActions, ids().at("GRID_FILTER_IGNORE_IN_ACTIONS"));
    EXPECT_EQ(kGlobalQuit, ids().at("GLOBAL_QUIT"));
}

TEST(Hotkeys, TypesFollowGetType)
{
    EXPECT_EQ(hotkeyType(1000), AudioHotkey);
    EXPECT_EQ(hotkeyType(1999), AudioHotkey);
    EXPECT_EQ(hotkeyType(2000), VideoHotkey);
    EXPECT_EQ(hotkeyType(3000), EditorHotkey);
    EXPECT_EQ(hotkeyType(3119), EditorHotkey); // tag buttons
    EXPECT_EQ(hotkeyType(4001), GridHotkey);
    EXPECT_EQ(hotkeyType(4556), GridHotkey);
    EXPECT_EQ(hotkeyType(5000), GlobalHotkey);
    EXPECT_EQ(hotkeyType(30100), GlobalHotkey);
    EXPECT_EQ(hotkeyWindowName(GridHotkey), "Subtitles");
}

TEST(Hotkeys, StoredLinesAreSaveHkeys)
{
    HotkeyMap map = defaultHotkeys();
    map[HotkeyId{kSave, GridHotkey}] = Hotkey{"Save", "F7"};            // copied to another window
    map[HotkeyId{kHistory, GlobalHotkey}].accel.clear();                 // deleted: not written
    map[HotkeyId{30100, GlobalHotkey}] = Hotkey{"Script a.lua-1", "Ctrl-K"}; // the automation hotkeys'
    map[HotkeyId{7000, VideoHotkey}] = Hotkey{"", "F12"};                // no symbol: its number
    map[HotkeyId{ids().at("AUDIO_PLAY"), GlobalHotkey}] = Hotkey{"", "Ctrl-P"}; // main file: not an Audio binding
    const auto main = hotkeyLines(map, false);
    const auto audio = hotkeyLines(map, true);
    EXPECT_EQ(main.size(), 62u + 2u - 1u + 1u + 12u + 9u); // WS3: no Ctrl-E; FS1: Tab; SH1: no F10; VT2, VT3: the tools; TM1: the run keys
    EXPECT_EQ(audio.size(), 23u + 3u + 2u); // KA2's three, KA1's K and Shift-K
    EXPECT_EQ(main.front(), "AUDIO_PLAY G=Ctrl-P"); // map order: window, then id
    EXPECT_NE(std::ranges::find(main, "GLOBAL_SAVE_SUBS G=Ctrl-S"), main.end());
    EXPECT_NE(std::ranges::find(main, "GLOBAL_SAVE_SUBS S=F7"), main.end());
    EXPECT_NE(std::ranges::find(main, "7000 V=F12"), main.end());
    EXPECT_NE(std::ranges::find(main, "VIDEO_VOLUME_MINUS V=Num 0"), main.end());
    EXPECT_EQ(std::ranges::find(main, "GLOBAL_HISTORY G="), main.end());
    for (const auto &l : main)
        EXPECT_FALSE(l.starts_with("Script")) << l;
    EXPECT_EQ(audio.front(), "AUDIO_COMMIT_ALT A=G");

    HotkeyMap read;
    readHotkeyLines(read, main);
    readHotkeyLines(read, audio);
    for (const auto &[k, h] : map)
        if (!h.accel.empty() && k.id < kFirstScriptHotkey) {
            EXPECT_EQ(read.at(k).accel, h.accel) << k.id;
        }
    EXPECT_FALSE(read.contains(HotkeyId{kHistory, GlobalHotkey}));
    // LoadHkeys: unknown symbols are id 0, unknown letters window -1, empty
    // bindings and script lines are skipped, spaces trimmed.
    HotkeyMap odd;
    readHotkeyLines(odd, {"  GLOBAL_SAVE_SUBS G=Ctrl-K \r", "NOPE G=F1", "GLOBAL_UNDO X=F2", "GLOBAL_REDO G=",
                          "Script a.lua-1=Ctrl-J", "5300 G=F6", ""});
    EXPECT_EQ(odd.size(), 4u);
    EXPECT_EQ(odd.at(HotkeyId{kSave, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(odd.at(HotkeyId{0, GlobalHotkey}).accel, "F1");
    EXPECT_EQ(odd.at(HotkeyId{ids().at("GLOBAL_UNDO"), -1}).accel, "F2");
    EXPECT_EQ(odd.at(HotkeyId{5300, GlobalHotkey}).accel, "F6");
}

TEST(Hotkeys, AcceleratorTextBecomesQtSequences)
{
    EXPECT_EQ(qtKeysOfAccel("Ctrl-Shift-O"), "Ctrl+Shift+O");
    EXPECT_EQ(qtKeysOfAccel("Alt-Space"), "Alt+Space");
    EXPECT_EQ(qtKeysOfAccel("Enter"), "Return");
    EXPECT_EQ(qtKeysOfAccel("Shift-Enter"), "Shift+Return");
    EXPECT_EQ(qtKeysOfAccel("Num Enter"), "Num+Enter");
    EXPECT_EQ(qtKeysOfAccel("Num 0"), "Num+0");
    EXPECT_EQ(qtKeysOfAccel("Num ."), "Num+.");
    EXPECT_EQ(qtKeysOfAccel("Num +"), "Num++");
    EXPECT_EQ(qtKeysOfAccel("Shift-Delete"), "Shift+Del");
    EXPECT_EQ(qtKeysOfAccel("Ctrl-PgDn"), "Ctrl+PgDown");
    EXPECT_EQ(qtKeysOfAccel("Ctrl-,"), "Ctrl+,");
    EXPECT_EQ(qtKeysOfAccel("Ctrl--"), "Ctrl+-");
    EXPECT_EQ(qtKeysOfAccel(";"), ";");
    EXPECT_EQ(qtKeysOfAccel("Ctrl-F12"), "Ctrl+F12");
    // GetHKey's quirks: "Num -" ends with '-', so it is the '-' key; the
    // modifiers are found anywhere; an unknown name installs nothing.
    EXPECT_EQ(qtKeysOfAccel("Num -"), "-");
    EXPECT_EQ(qtKeysOfAccel("Shift-Ctrl-X"), "Ctrl+Shift+X");
    EXPECT_EQ(qtKeysOfAccel("Ctrl-Bogus"), "");
    EXPECT_EQ(qtKeysOfAccel("Num |"), ""); // no Qt key
    EXPECT_EQ(qtKeysOfAccel(""), "");

    EXPECT_EQ(accelOfQtKeys("Ctrl+Shift+A"), "Ctrl-Shift-A");
    EXPECT_EQ(accelOfQtKeys("Shift+Alt+Ctrl+A"), "Alt-Ctrl-Shift-A");
    EXPECT_EQ(accelOfQtKeys("Return"), "Enter");
    EXPECT_EQ(accelOfQtKeys("Num+Enter"), "Num Enter");
    EXPECT_EQ(accelOfQtKeys("Num+0"), "Num 0");
    EXPECT_EQ(accelOfQtKeys("Num++"), "Num +");
    EXPECT_EQ(accelOfQtKeys("Ctrl++"), "Ctrl-+");
    EXPECT_EQ(accelOfQtKeys("Shift+Del"), "Shift-Delete");
    EXPECT_EQ(accelOfQtKeys("PgDown"), "PgDn");
    for (const char *a : {"Ctrl-Shift-O", "Alt-Space", "Enter", "Num 0", "Num Enter", "Ctrl-,", "F9", "Ctrl-PgUp"})
        EXPECT_EQ(accelOfQtKeys(qtKeysOfAccel(a)), a);
}

TEST(Hotkeys, MappingWindowReadsKeysAsOnKeyPress)
{
    using R = HotkeyCapture::Result;
    const auto press = [](int key, bool alt, bool ctrl, bool shift) {
        return HotkeyPress{key, alt, ctrl, shift, ctrl && !alt && !shift};
    };
    // A lone modifier waits.
    EXPECT_EQ(captureHotkey(press(wxk::Shift, false, false, true), GlobalHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press(wxk::Control, false, true, false), GlobalHotkey).result, R::Ignored);
    // Modifiers in legacy order.
    auto c = captureHotkey(press('K', true, true, true), GlobalHotkey);
    EXPECT_EQ(c.result, R::Accepted);
    EXPECT_EQ(c.accel, "Alt-Ctrl-Shift-K");
    // Global and Editor keys 31..126 need a modifier; the others do not.
    c = captureHotkey(press('K', false, false, false), GlobalHotkey);
    EXPECT_EQ(c.result, R::Refused);
    EXPECT_EQ(c.message, "Global and editor shortcuts must include modifiers (e.g. Shift, Ctrl, Alt).");
    EXPECT_EQ(captureHotkey(press('K', false, false, false), EditorHotkey).result, R::Refused);
    EXPECT_EQ(captureHotkey(press(wxk::Space, false, false, false), GlobalHotkey).result, R::Refused);
    EXPECT_EQ(captureHotkey(press('K', false, false, false), VideoHotkey).accel, "K");
    EXPECT_EQ(captureHotkey(press('K', false, false, false), GridHotkey).accel, "K");
    EXPECT_EQ(captureHotkey(press(wxk::F1 + 6, false, false, false), GlobalHotkey).accel, "F7");
    EXPECT_EQ(captureHotkey(press(wxk::Numpad0 + 3, false, false, false), GlobalHotkey).accel, "Num 3");
    EXPECT_EQ(captureHotkey(press(wxk::Delete, false, false, false), GlobalHotkey).accel, "Delete");
    EXPECT_EQ(captureHotkey(press(wxk::Return, false, false, false), EditorHotkey).accel, "Enter");
    // Ctrl alone with V, C, X or Z, and Alt+F4 without Ctrl, are refused.
    c = captureHotkey(press('V', false, true, false), VideoHotkey);
    EXPECT_EQ(c.result, R::Refused);
    EXPECT_EQ(c.message, "You cannot use shortcuts for copying, cutting, and pasting.");
    EXPECT_EQ(captureHotkey(press('Z', false, true, false), GridHotkey).result, R::Refused);
    EXPECT_EQ(captureHotkey(press('V', false, true, true), GridHotkey).accel, "Ctrl-Shift-V");
    c = captureHotkey(press(wxk::F4, true, false, false), VideoHotkey);
    EXPECT_EQ(c.message, "You cannot use the program exit shortcut.");
    EXPECT_EQ(captureHotkey(press(wxk::F4, true, true, false), VideoHotkey).accel, "Alt-Ctrl-F4");
    // Keys outside FillTable and '$'..'`' are ignored (the window stays).
    EXPECT_EQ(captureHotkey(press(wxk::Escape, false, false, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press('#', false, true, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press('a', false, true, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press(0x0105, false, true, false), VideoHotkey).result, R::Ignored); // ą
    EXPECT_EQ(captureHotkey(press(wxk::Home + 62, false, true, false), VideoHotkey).result, R::Ignored); // numpad home
    EXPECT_EQ(captureHotkey(press(';', false, false, false), VideoHotkey).accel, ";");
    EXPECT_EQ(captureHotkey(press('`', false, true, false), VideoHotkey).accel, "Ctrl-`");
}

TEST(Hotkeys, TextFieldKeepsItsOwnKeys)
{
    for (const char *a : {"Enter", "Delete", "Backspace", "Ctrl-Backspace", "Left", "Shift-Up", "Ctrl-Shift-Left",
                          "Ctrl-A", "Ctrl-C", "Ctrl-V", "Ctrl-X", "Shift-Ctrl-Right"})
        EXPECT_TRUE(textFieldOwnsKey(a, true)) << a;
    for (const char *a : {"Ctrl-Enter", "Shift-Enter", "Ctrl-B", "Alt-Down", "Ctrl-Up", "Num 1", "Num Enter"})
        EXPECT_FALSE(textFieldOwnsKey(a, true)) << a;
    EXPECT_TRUE(textFieldOwnsKey("Num 1", false)); // TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS off
    EXPECT_FALSE(textFieldOwnsKey("Ctrl-Num 1", false));
}

TEST(Hotkeys, ListOrderIsAddHotkeysOnList)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    auto live = defaultHotkeys();
    list.build(live);
    // The names table from the highest id down; no GLOBAL_QUIT.
    // V3, AM1: no "Open video with FFMS2" or "Open shortcut mapping window"
    // row; WS3: none for the five View arrangements and the editor switch;
    // LE3's, KE1's, FS1's, TM1's (the run keys too), VT2's and VT3's commands, KA2's three, KA7's, KA1's two, AD2's, KA3's and FR1's have their own;
    // #385 names Hide line wraps, which legacy left without a name.
    ASSERT_EQ(list.rowCount(), 254u);
    EXPECT_EQ(list.row(0).text, "Global Close current tab");
    EXPECT_EQ(list.row(0).accel, "Ctrl-W");
    EXPECT_EQ(rowNamed(list, "Global Hide tags in the line editor").accel, "");
    EXPECT_EQ(rowNamed(list, "Global Show / hide render cost").accel, "");
    EXPECT_EQ(rowNamed(list, "Video Full screen bar: Auto / Always / Never").accel, "Tab");
    EXPECT_EQ(rowNamed(list, "Audio Tap to time").accel, "Y"); // KA2
    for (const auto &tool : kToolCommands)
        EXPECT_EQ(rowNamed(list, std::string("Video ") + tool.name).accel, tool.accel);
    EXPECT_EQ(rowNamed(list, "Global Check karaoke").accel, "");
    EXPECT_EQ(rowNamed(list, "Global Kanji timer").accel, "");
    EXPECT_EQ(rowNamed(list, "Global Getting started").accel, "");
    EXPECT_EQ(rowNamed(list, "Subtitles Hide line wraps").key.id, 4513); // #385
    EXPECT_EQ(list.row(int(list.rowCount()) - 1).text, "Audio Commit alt");
    EXPECT_EQ(list.find("Global Exit"), -1);
    EXPECT_EQ(list.selection(), 0);
    EXPECT_FALSE(list.modified());
    int last = -1;
    for (std::size_t i = 0; i < list.rowCount(); ++i) {
        const auto &r = list.row(int(i));
        EXPECT_GE(r.key.type, last); // grouped Global, Subtitles, Editor, Video, Audio
        last = r.key.type;
        EXPECT_EQ(r.text, std::string(hotkeyWindowName(r.key.type)) + " " + r.name);
    }
    EXPECT_EQ(rowNamed(list, "Video Volume down").accel, "Num 0");
    EXPECT_EQ(rowNamed(list, "Editor First tag button").accel, "");

    // Bindings not listed by name follow their window's names: a script and
    // a Save copied to the Subtitles window.
    live[HotkeyId{30100, GlobalHotkey}] = Hotkey{"Script a.lua-1", "Ctrl-K"};
    live[HotkeyId{kSave, GridHotkey}] = Hotkey{"Save", "F7"};
    HotkeyList more(copy);
    more.build(live);
    ASSERT_EQ(more.rowCount(), 256u); // V3, AM1, WS3, TM1, VT2, KA2, VT3, KA7, KA1, AD2, KA3, FR1, #385: 254 named rows and these two
    const int script = more.find("Global Script a.lua-1");
    const int copied = more.find("Subtitles Save");
    ASSERT_GE(script, 0);
    ASSERT_GE(copied, 0);
    EXPECT_EQ(more.row(script - 1).key.type, GlobalHotkey);
    EXPECT_EQ(more.row(script + 1).key.type, GridHotkey);
    EXPECT_EQ(more.row(copied + 1).key.type, EditorHotkey);
    EXPECT_EQ(more.row(copied).accel, "F7");

    // Once every binding is listed, the names left are not: without the last
    // Audio binding (Commit alt, id 1000) its row is missing.
    auto fewer = defaultHotkeys();
    fewer.erase(HotkeyId{kAudioCommitAlt, AudioHotkey});
    HotkeyList cut(copy);
    cut.build(fewer);
    EXPECT_EQ(cut.rowCount(), 253u); // V3, AM1, WS3, TM1, VT2, KA2, VT3, KA7, KA1, AD2, KA3, FR1, #385: 254 named rows less Commit alt
    EXPECT_EQ(cut.find("Audio Commit alt"), -1);
    // A binding in a window legacy cannot name (-1) is never listed, and so
    // the list never ends early.
    auto odd = fewer;
    odd[HotkeyId{5001, -1}] = Hotkey{"", "F1"};
    HotkeyList whole(copy);
    whole.build(odd);
    EXPECT_EQ(whole.rowCount(), 254u);
}

TEST(Hotkeys, FilterModesAndSelection)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const auto count = [&](int mode) {
        list.filter(mode);
        return list.shown().size();
    };
    EXPECT_EQ(count(1), 110u); // set shortcuts (GLOBAL_QUIT is not listed; WS3: no Ctrl-E; FS1: Tab; SH1: no F10; VT2: 11; KA2: three; VT3: Q; TM1: nine; KA1: K, Shift-K)
    EXPECT_EQ(count(3), std::size_t(std::ranges::count_if(hotkeyNames(), [](const auto &n) { return hotkeyType(n.first) == GridHotkey; })));
    EXPECT_EQ(count(6), 28u);
    list.filter(5);
    EXPECT_EQ(list.row(list.selection()).key.type, VideoHotkey); // the first shown
    list.selectShown(2);
    EXPECT_EQ(list.selection(), list.shown()[2]);
    // A row deleted under "Set shortcuts" stays shown until filtered again.
    list.filter(1);
    const int first = list.selection();
    list.remove(first, live);
    EXPECT_EQ(list.shown().size(), 110u);
    EXPECT_EQ(list.row(first).accel, "");
    list.filter(1);
    EXPECT_EQ(list.shown().size(), 109u);
    list.filter(0);
    EXPECT_EQ(list.shown().size(), 254u);
}

// PF1 (#225): the Hotkeys page's search, within the filter, by the
// Function or Hotkey column in any case (the macros' rows also by script
// and macro, `also`).
TEST(Hotkeys, SearchNarrowsTheFilter)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    list.search("gLOBAL hISTORY");
    ASSERT_EQ(list.shown().size(), 1u);
    EXPECT_EQ(list.row(list.selection()).text, "Global History");
    list.search("ctrl-shift-h"); // the Hotkey column
    EXPECT_TRUE(std::ranges::any_of(list.shown(), [&](int i) { return list.row(i).text == "Global History"; }));
    // Within the filter: History is no Video shortcut.
    list.filter(5);
    EXPECT_TRUE(std::ranges::none_of(list.shown(), [&](int i) { return list.row(i).text == "Global History"; }));
    list.filter(0);
    list.search("no such function");
    EXPECT_TRUE(list.shown().empty());
    list.search("my script", [](const HotkeyList::Row &r) { return r.text == "Global Save" ? "My Script" : ""; });
    ASSERT_EQ(list.shown().size(), 1u);
    EXPECT_EQ(list.row(list.shown()[0]).text, "Global Save");
    list.search("");
    EXPECT_EQ(list.shown().size(), list.rowCount()); // every row again
}

// PF1-staged-reset: "Reset page" on the Hotkeys page, every row back to
// its default in one step of the history.
TEST(Hotkeys, ResetAllIsOneStep)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const int history = list.find("Global History");
    const int open = list.find("Global Open subtitles");
    list.map(history, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
    list.remove(open, live);
    list.filter(5); // History and Open hidden by the filter
    list.resetAll(live);
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    EXPECT_EQ(copy.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-H");
    EXPECT_EQ(list.row(history).visible, 0); // still filtered out
    list.undo();
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-J");
    EXPECT_EQ(list.row(open).accel, "");
    list.redo();
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
}

// A search by the Hotkey column follows the reset bindings.
TEST(Hotkeys, ResetAllRerunsTheSearch)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const int history = list.find("Global History");
    list.map(history, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
    list.search("ctrl-shift-j");
    ASSERT_TRUE(std::ranges::any_of(list.shown(), [&](int i) { return i == history; }));
    list.resetAll(live);
    EXPECT_TRUE(std::ranges::none_of(list.shown(), [&](int i) { return i == history; }));
    list.search("ctrl-shift-h");
    EXPECT_TRUE(std::ranges::any_of(list.shown(), [&](int i) { return i == history; }));
}

// So do Map hotkey, Delete, Undo and Redo; the selection stays on the
// edited row while it still matches.
TEST(Hotkeys, EditsRerunTheSearch)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const int history = list.find("Global History");
    const auto shown = [&](int row) { return std::ranges::any_of(list.shown(), [&](int i) { return i == row; }); };
    list.search("history");
    list.map(history, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
    EXPECT_TRUE(shown(history)); // still "History"
    EXPECT_EQ(list.selection(), history);
    list.search("ctrl-shift-j");
    ASSERT_TRUE(shown(history));
    list.remove(history, live);
    EXPECT_FALSE(shown(history));
    list.undo();
    EXPECT_TRUE(shown(history));
    list.redo();
    EXPECT_FALSE(shown(history));
    list.undo();
    list.map(history, "Ctrl-Shift-K", GlobalHotkey, HotkeyAnswer::Cancel, live);
    EXPECT_FALSE(shown(history));
}

TEST(Hotkeys, MapSetsTheBindingAndCommitWritesTheCopy)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const int history = list.find("Global History");
    EXPECT_EQ(list.mapName(history), "History");
    EXPECT_TRUE(list.mapOffersWindows(history));
    EXPECT_FALSE(list.mapConflict(history, "Ctrl-Shift-J", GlobalHotkey, live));
    EXPECT_EQ(copy, live); // filled at the first edit
    list.map(history, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-J");
    EXPECT_TRUE(list.row(history).keyModified);
    EXPECT_TRUE(list.row(history).textModified);
    EXPECT_TRUE(list.modified());
    auto committed = list.commit();
    ASSERT_TRUE(committed);
    EXPECT_EQ(committed->at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-J");
    EXPECT_FALSE(list.row(history).keyModified);
    EXPECT_FALSE(list.modified());
    EXPECT_FALSE(list.commit()); // nothing modified since
}

TEST(Hotkeys, ConflictsPerWindow)
{
    const auto live = defaultHotkeys();
    {
        // The same window: "already exists for", Switch / Delete / Cancel.
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(live);
        const int history = list.find("Global History");
        auto c = list.mapConflict(history, "Ctrl-S", GlobalHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        EXPECT_EQ(c->message, "This hotkey already exists for \"Save\".\nWhat to do?");
        // PK1's hotkey capture names the commands alone.
        EXPECT_EQ(c->commands, std::vector<std::string>{"Save"});
        EXPECT_TRUE(c->canSwitch);
        EXPECT_FALSE(c->canSetAnyway);
        list.map(history, "Ctrl-S", GlobalHotkey, HotkeyAnswer::Cancel, live);
        EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
        list.map(history, "Ctrl-S", GlobalHotkey, HotkeyAnswer::Switch, live);
        EXPECT_EQ(list.row(history).accel, "Ctrl-S");
        EXPECT_EQ(rowNamed(list, "Global Save").accel, "Ctrl-Shift-H"); // switched
        EXPECT_EQ(copy.at(HotkeyId{kSave, GlobalHotkey}).accel, "Ctrl-Shift-H");
        const int open = list.find("Global Open subtitles");
        list.map(open, "Ctrl-S", GlobalHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(history).accel, "");
        EXPECT_EQ(copy.at(HotkeyId{kHistory, GlobalHotkey}).accel, "");
        EXPECT_EQ(list.row(open).accel, "Ctrl-S");
    }
    {
        // Another window: "Set anyway" keeps both; one other binding can be switched.
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(live);
        const int dup = list.find("Subtitles Duplicate lines");
        auto c = list.mapConflict(dup, "Ctrl-R", GridHotkey, live); // Editor "Next untranslated line"
        ASSERT_TRUE(c);
        EXPECT_FALSE(c->doubled);
        EXPECT_EQ(c->message, "This shortcut already exists in another window as a shortcut for \"Editor Next "
                              "untranslated line\".\nWhat would you like to do?");
        EXPECT_TRUE(c->canSwitch);
        EXPECT_TRUE(c->canSetAnyway);
        list.map(dup, "Ctrl-R", GridHotkey, HotkeyAnswer::SetAnyway, live);
        EXPECT_EQ(list.row(dup).accel, "Ctrl-R");
        EXPECT_EQ(rowNamed(list, "Editor Next untranslated line").accel, "Ctrl-R");
        // Two other windows: no Switch.
        copy[HotkeyId{kSave, VideoHotkey}] = Hotkey{"Save", "Ctrl-R"};
        const int history = list.find("Global History");
        c = list.mapConflict(history, "Ctrl-R", GlobalHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_EQ(c->count, 3u);
        EXPECT_FALSE(c->canSwitch);
        EXPECT_EQ(c->message, "This shortcut already exists in other windows as a shortcut for \"Subtitles Duplicate "
                              "lines, Editor Next untranslated line, Video Save\".\nWhat would you like to do?");
        EXPECT_EQ(c->commands, (std::vector<std::string>{"Duplicate lines", "Next untranslated line", "Save"}));
        // Delete clears every binding found by its row (the Video copy has none).
        list.map(history, "Ctrl-R", GlobalHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(dup).accel, "");
        EXPECT_EQ(rowNamed(list, "Editor Next untranslated line").accel, "");
        EXPECT_EQ(copy.at(HotkeyId{kSave, VideoHotkey}).accel, "Ctrl-R");
    }
    {
        // Video and Audio count as the same window; so does a video play or
        // seek id against any window but Global. Only same-window bindings
        // change.
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(live);
        const int stop = list.find("Video Stop");
        // Audio's W (VT2: H, Audio Stop's, is Hydra's in the Video window; VT3: Q, Play 500ms before's, is Perspective's).
        auto c = list.mapConflict(stop, "W", VideoHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        EXPECT_EQ(c->message, "This hotkey already exists for \"Play 500ms after\".\nWhat to do?");
        list.map(stop, "W", VideoHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(stop).accel, "W");
        EXPECT_EQ(rowNamed(list, "Audio Play 500ms after").accel, "W"); // another window: untouched
        const int insert = list.find("Subtitles Insert before");
        c = list.mapConflict(insert, "L", GridHotkey, live); // Video 5 seconds forward
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        const int newTab = list.find("Global Open new tab");
        c = list.mapConflict(newTab, "Shift-L", GlobalHotkey, live);
        EXPECT_FALSE(c);
    }
    {
        // A conflict is found by the copy but changed through its row: a
        // script's has none, and a shared name finds the first such row.
        auto withScript = live;
        withScript[HotkeyId{30100, GlobalHotkey}] = Hotkey{"Script a.lua-1", "Ctrl-K"};
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(withScript);
        const int history = list.find("Global History");
        auto c = list.mapConflict(history, "Ctrl-K", GlobalHotkey, withScript);
        ASSERT_TRUE(c);
        EXPECT_EQ(c->message, "This hotkey already exists for \"Script a.lua-1\".\nWhat to do?");
        list.map(history, "Ctrl-K", GlobalHotkey, HotkeyAnswer::Delete, withScript);
        EXPECT_EQ(copy.at(HotkeyId{30100, GlobalHotkey}).accel, "Ctrl-K"); // kept
        EXPECT_EQ(rowNamed(list, "Global Script a.lua-1").accel, "Ctrl-K");
        EXPECT_EQ(list.row(history).accel, "Ctrl-K");
    }
}

TEST(Hotkeys, MapToAnotherWindowAddsOrUpdatesItsRow)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const int save = list.find("Global Save");
    list.map(save, "F7", VideoHotkey, HotkeyAnswer::Cancel, live);
    EXPECT_EQ(list.row(save).accel, "Ctrl-S"); // the Global row keeps its keys
    ASSERT_EQ(list.rowCount(), 255u);
    const auto &added = list.row(254);
    EXPECT_EQ(added.text, "Video Save");
    EXPECT_EQ((added.key), (HotkeyId{kSave, VideoHotkey}));
    EXPECT_EQ(added.accel, "F7");
    EXPECT_EQ(list.selection(), -1); // SetSelection(GetCount()) selects nothing
    EXPECT_EQ(copy.at(HotkeyId{kSave, VideoHotkey}).accel, "F7");
    // Again: that row changes and is selected.
    list.map(save, "F6", VideoHotkey, HotkeyAnswer::Cancel, live);
    EXPECT_EQ(list.rowCount(), 255u);
    EXPECT_EQ(list.row(254).accel, "F6");
    EXPECT_EQ(list.selection(), 254);
}

TEST(Hotkeys, ResetDeleteUndoRedo)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    const int history = list.find("Global History");
    const int open = list.find("Global Open subtitles");
    list.map(history, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
    list.map(open, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Switch, live); // History gets Ctrl-O
    EXPECT_EQ(list.row(history).accel, "Ctrl-O");
    // Restore default asks nothing about the keys it gives back.
    list.reset(history, live);
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    list.reset(open, live);
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    list.remove(open, live);
    EXPECT_EQ(list.row(open).accel, "");
    EXPECT_TRUE(list.row(open).keyModified);
    EXPECT_EQ(copy.at(HotkeyId{5100, GlobalHotkey}).accel, "");
    // Undo restores the rows and writes them into the copy.
    list.undo();
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    EXPECT_EQ(copy.at(HotkeyId{5100, GlobalHotkey}).accel, "Ctrl-O");
    list.undo();
    list.undo();
    EXPECT_EQ(list.row(history).accel, "Ctrl-O");
    EXPECT_EQ(list.row(open).accel, "Ctrl-Shift-J");
    EXPECT_EQ(copy.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-O");
    list.redo();
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    list.undo();
    list.undo();
    list.undo();
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    list.undo(); // at the start: nothing
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    // An edit drops the redo branch.
    list.remove(history, live);
    list.redo();
    EXPECT_EQ(list.row(history).accel, "");
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    // A row added for another window goes on undo; its binding stays in the copy.
    const int save = list.find("Global Save");
    list.map(save, "F7", VideoHotkey, HotkeyAnswer::Cancel, live);
    list.undo();
    EXPECT_EQ(list.find("Video Save"), -1);
    EXPECT_EQ(copy.at(HotkeyId{kSave, VideoHotkey}).accel, "F7");
    const auto committed = list.commit();
    ASSERT_TRUE(committed);
    EXPECT_EQ(committed->at(HotkeyId{kSave, VideoHotkey}).accel, "F7");
}

TEST(Hotkeys, TheCopyOutlivesTheDialogAndSetDefault)
{
    // Legacy's hotkeysCopy is static and filled only while empty: a later
    // dialog's OK writes what an earlier, cancelled one left in it, and
    // reverts bindings changed since by the Shift+click gesture.
    HotkeyMap copy;
    auto live = defaultHotkeys();
    {
        HotkeyList first(copy);
        first.build(live);
        first.map(first.find("Global History"), "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
        // Cancel: nothing committed.
    }
    mapHotkeyNow(live, kDuplicate, "Duplicate lines", "Ctrl-Shift-D", GridHotkey, HotkeyAnswer::Cancel);
    HotkeyList second(copy);
    second.build(live);
    EXPECT_EQ(rowNamed(second, "Global History").accel, "Ctrl-Shift-H"); // the list shows the live bindings
    EXPECT_EQ(rowNamed(second, "Subtitles Duplicate lines").accel, "Ctrl-Shift-D");
    second.remove(second.find("Global Save"), live);
    const auto committed = second.commit();
    ASSERT_TRUE(committed);
    EXPECT_EQ(committed->at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-J"); // the cancelled edit
    EXPECT_EQ(committed->at(HotkeyId{kDuplicate, GridHotkey}).accel, "Ctrl-D");      // the gesture reverted
    EXPECT_EQ(committed->at(HotkeyId{kSave, GlobalHotkey}).accel, "");

    // "Set default" (ResetDefault): the live bindings take the defaults, the
    // list is built again from them, its modified flag stays, and the copy
    // keeps the edits. The history starts again (O2-reset-history).
    HotkeyList third(copy);
    third.build(*committed);
    third.map(third.find("Global Open subtitles"), "F7", GlobalHotkey, HotkeyAnswer::Cancel, *committed);
    third.filter(2);
    auto reset = defaultHotkeys();
    third.clear();
    third.build(reset);
    EXPECT_EQ(third.shown().size(), 254u); // every row shown, the filter choice as it was
    EXPECT_EQ(third.filterMode(), 2);
    EXPECT_EQ(rowNamed(third, "Global Open subtitles").accel, "Ctrl-O");
    EXPECT_TRUE(third.modified());
    third.undo(); // nothing to undo
    EXPECT_EQ(rowNamed(third, "Global Open subtitles").accel, "Ctrl-O");
    third.remove(third.find("Global Find"), reset);
    third.undo();
    EXPECT_EQ(rowNamed(third, "Global Find").accel, "Ctrl-F");
    const auto after = third.commit();
    ASSERT_TRUE(after);
    EXPECT_EQ(after->at(HotkeyId{5100, GlobalHotkey}).accel, "F7");       // from the copy
    EXPECT_EQ(after->at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-J");
}

TEST(Hotkeys, GestureMapsAtOnce)
{
    auto live = defaultHotkeys();
    // Every binding with the keys counts, the one mapped included.
    auto c = mapHotkeyNowConflict(live, kSave, "Ctrl-S", GlobalHotkey);
    ASSERT_TRUE(c);
    EXPECT_TRUE(c->doubled);
    EXPECT_EQ(c->message, "This hotkey already exists for \"Save\".\nWhat to do?");
    // Only the same window is "the same" here (no Video/Audio rule). Audio's
    // W (VT2: H, Audio Stop's, is Hydra's in the Video window; VT3: Q, Play 500ms before's, is Perspective's).
    c = mapHotkeyNowConflict(live, ids().at("VIDEO_STOP"), "W", VideoHotkey);
    ASSERT_TRUE(c);
    EXPECT_FALSE(c->doubled);
    EXPECT_EQ(c->message, "This shortcut already exists in another window as a shortcut for \"Audio Play 500ms "
                          "after\".\nWhat would you like to do?");
    auto r = mapHotkeyNow(live, ids().at("VIDEO_STOP"), "Stop", "W", VideoHotkey, HotkeyAnswer::SetAnyway);
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.saveAudio); // an Audio binding was named
    EXPECT_EQ(live.at(HotkeyId{ids().at("VIDEO_STOP"), VideoHotkey}).accel, "W");
    EXPECT_EQ(live.at(HotkeyId{ids().at("AUDIO_PLAY_500MS_AFTER"), AudioHotkey}).accel, "W");
    // Switch gives the other binding the old keys of (id, window).
    r = mapHotkeyNow(live, kNextDoubtful, "Next unconfirmed line", "Ctrl-R", EditorHotkey, HotkeyAnswer::Switch);
    EXPECT_FALSE(r.saveAudio);
    EXPECT_EQ(live.at(HotkeyId{kNextDoubtful, EditorHotkey}).accel, "Ctrl-R");
    EXPECT_EQ(live.at(HotkeyId{3018, EditorHotkey}).accel, "Ctrl-D");
    // Delete, and a new window's binding with the name given.
    r = mapHotkeyNow(live, kSave, "Save", "Ctrl-D", GridHotkey, HotkeyAnswer::Delete);
    EXPECT_EQ(live.at(HotkeyId{kDuplicate, GridHotkey}).accel, "");
    EXPECT_EQ(live.at(HotkeyId{3018, EditorHotkey}).accel, "Ctrl-D"); // another window
    EXPECT_EQ(live.at(HotkeyId{kSave, GridHotkey}).name, "Save");
    // Cancel changes nothing.
    const auto before = live;
    r = mapHotkeyNow(live, kHistory, "History", "Ctrl-S", GlobalHotkey, HotkeyAnswer::Cancel);
    EXPECT_FALSE(r.changed);
    EXPECT_EQ(live, before);
    // An Audio binding saves the audio file too.
    r = mapHotkeyNow(live, ids().at("AUDIO_GOTO"), "Go to selection", "F12", AudioHotkey, HotkeyAnswer::Cancel);
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.saveAudio);
}

// Hotkeys::OnMapHkey's Switch (Hotkeys.cpp:497-509), in map order: each
// binding holding the keys is cleared and given (id, window)'s keys as they
// are then. The mapped binding among them clears itself, so those after it
// get nothing; SetHKey gives it the keys afterwards.
TEST(Hotkeys, GestureSwitchFollowsLegacyOrder)
{
    HotkeyMap live;
    live[HotkeyId{5005, GlobalHotkey}] = Hotkey{"", "Ctrl-K"};  // GLOBAL_REDO, before it
    live[HotkeyId{kHistory, GlobalHotkey}] = Hotkey{"", "Ctrl-K"}; // the binding mapped
    live[HotkeyId{5100, GlobalHotkey}] = Hotkey{"", "Ctrl-K"};  // GLOBAL_OPEN_SUBS, after it
    live[HotkeyId{kDuplicate, GridHotkey}] = Hotkey{"", "Ctrl-K"}; // another window: not doubled, kept
    const auto c = mapHotkeyNowConflict(live, kHistory, "Ctrl-K", GlobalHotkey);
    ASSERT_TRUE(c);
    EXPECT_TRUE(c->doubled);
    const auto r = mapHotkeyNow(live, kHistory, "History", "Ctrl-K", GlobalHotkey, HotkeyAnswer::Switch);
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(live.at(HotkeyId{5005, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{5100, GlobalHotkey}).accel, "");
    EXPECT_EQ(live.at(HotkeyId{kDuplicate, GridHotkey}).accel, "Ctrl-K");
    // Without the mapped binding among them, every doubled one gets its old keys.
    live[HotkeyId{5100, GlobalHotkey}].accel = "Ctrl-J";
    live[HotkeyId{5005, GlobalHotkey}].accel = "Ctrl-J";
    mapHotkeyNow(live, kHistory, "History", "Ctrl-J", GlobalHotkey, HotkeyAnswer::Switch);
    EXPECT_EQ(live.at(HotkeyId{5005, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{5100, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-J");
}

// Hotkeys::GetHKey (Hotkeys.cpp:362-375): a key text that is no FillTable
// name and longer than one character (UTF-16 units) is logged as invalid.
TEST(Hotkeys, InvalidKeysAreReportedAsGetHKeyLogsThem)
{
    EXPECT_EQ(invalidHotkeyKey("Ctrl-Bogus"), "Bogus");
    EXPECT_EQ(invalidHotkeyKey("Ctrl-Shift"), "Shift");
    EXPECT_EQ(invalidHotkeyKey("Num 0"), "");
    EXPECT_EQ(invalidHotkeyKey("Num -"), ""); // the '-' key
    EXPECT_EQ(invalidHotkeyKey("Ctrl-A"), "");
    EXPECT_EQ(invalidHotkeyKey("Alt-\xc3\xa9"), ""); // one character: valid for legacy
    EXPECT_EQ(qtKeysOfAccel("Alt-\xc3\xa9"), "");    // but no Qt sequence here
    EXPECT_EQ(invalidHotkeyKey("Alt-\xf0\x9f\x98\x80"), "\xf0\x9f\x98\x80"); // two UTF-16 units
    EXPECT_EQ(invalidHotkeyKey(""), "");
}

// LoadHkeys (Hotkeys.cpp:300-301): a numeric label is Labels.IsNumber()
// (a sign, then digits) read with wxAtoi, which never fails. Past the int
// range each legacy build reads it its own way (R5-per-platform): Windows'
// _wtoi clamps, Linux's atoi truncates a 64-bit strtol.
TEST(Hotkeys, NumericLabelsAreReadWithEachPlatformsWxAtoi)
{
    EXPECT_EQ(hotkeyLabelNumber("99999999999", true), 2147483647);
    EXPECT_EQ(hotkeyLabelNumber("99999999999", false), 1215752191);
    EXPECT_EQ(hotkeyLabelNumber("-99999999999", true), -2147483647 - 1);
    EXPECT_EQ(hotkeyLabelNumber("-99999999999", false), -1215752191);
    EXPECT_EQ(hotkeyLabelNumber("4294971297", true), 2147483647);
    EXPECT_EQ(hotkeyLabelNumber("4294971297", false), 4001); // GRID_HIDE_LAYER on Linux
    EXPECT_EQ(hotkeyLabelNumber("99999999999999999999999", true), 2147483647);
    EXPECT_EQ(hotkeyLabelNumber("99999999999999999999999", false), -1); // strtol saturates at LONG_MAX
    for (const bool windows : {true, false}) {
        EXPECT_EQ(hotkeyLabelNumber("+4527", windows), kDuplicate);
        EXPECT_EQ(hotkeyLabelNumber("-5", windows), -5);
        EXPECT_EQ(hotkeyLabelNumber("-", windows), 0);
        EXPECT_EQ(hotkeyLabelNumber("2147483647", windows), 2147483647);
    }
    for (const bool windows : {true, false}) {
        HotkeyMap map;
        EXPECT_NO_THROW(readHotkeyLines(
            map, {"99999999999 G=Ctrl-K", "4294971297 S=F1", "-5 S=F2", "+4527 S=F3", "12a G=F4"}, windows));
        EXPECT_EQ(map.at(HotkeyId{windows ? 2147483647 : 1215752191, GlobalHotkey}).accel, "Ctrl-K");
        EXPECT_EQ(map.contains(HotkeyId{4001, GridHotkey}), !windows);
        EXPECT_EQ(map.at(HotkeyId{-5, GridHotkey}).accel, "F2");
        EXPECT_EQ(map.at(HotkeyId{kDuplicate, GridHotkey}).accel, "F3");
        EXPECT_EQ(map.at(HotkeyId{0, GlobalHotkey}).accel, "F4"); // not a number: GetIdValue
        // An id under 100 or in the scripts' range is not written back (legacy
        // writes the latter as "=Ctrl-K", which it cannot read again).
        std::vector<std::string> expected{"GRID_DUPLICATE_LINES S=F3"};
        if (!windows)
            expected.insert(expected.begin(), "GRID_HIDE_LAYER S=F1");
        EXPECT_EQ(hotkeyLines(map, false), expected);
    }
    // The host's build: Windows on Windows, Linux elsewhere.
    HotkeyMap host;
    readHotkeyLines(host, {"99999999999 G=Ctrl-K"});
#ifdef _WIN32
    EXPECT_TRUE(host.contains(HotkeyId{2147483647, GlobalHotkey}));
#else
    EXPECT_TRUE(host.contains(HotkeyId{1215752191, GlobalHotkey}));
#endif
}

// HikariListCtrl's gutter (CheckIfHasHiddenBlock, ShowOrHideBlock) over
// ItemHotkey's filter.
TEST(Hotkeys, FilteredListShowsAndHidesBlocks)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    list.build(defaultHotkeys());
    EXPECT_FALSE(list.filtered());
    list.filter(1); // Set shortcuts
    ASSERT_TRUE(list.filtered());
    const auto shown = list.shown();
    // The first set shortcut is the first row (GLOBAL_CLOSE_PAGE, Ctrl-W);
    // rows without keys follow it, hidden.
    ASSERT_EQ(shown.front(), 0);
    EXPECT_EQ(list.hiddenBlock(-1), 0);
    int position = -1;
    for (int i = 0; i + 1 < int(shown.size()); ++i)
        if (shown[std::size_t(i) + 1] > shown[std::size_t(i)] + 1) {
            position = i;
            break;
        }
    ASSERT_GE(position, 0);
    const int first = shown[std::size_t(position)];
    const int next = shown[std::size_t(position) + 1];
    EXPECT_EQ(list.hiddenBlock(position), 1);
    list.toggleBlock(position);
    EXPECT_EQ(list.shown().size(), shown.size() + std::size_t(next - first - 1));
    EXPECT_EQ(list.row(first + 1).visible, 2);
    EXPECT_EQ(list.hiddenBlock(position), 2);
    list.toggleBlock(position);
    EXPECT_EQ(list.shown(), shown);
    // No block, no change; "All" is not filtered.
    list.toggleBlock(int(shown.size()) - 1);
    EXPECT_EQ(list.shown(), shown);
    list.filter(0);
    EXPECT_FALSE(list.filtered());
    EXPECT_EQ(list.hiddenBlock(0), 0);
}

// V3-indexing-retired: a stored binding of GLOBAL_VIDEO_INDEXING (a carried
// over Hotkeys.txt) is read, recognised and dropped for the notice; other
// bindings stay.
TEST(Hotkeys, RetiredIndexingBindingsAreDropped)
{
    HotkeyMap map;
    readHotkeyLines(map, {"GLOBAL_VIDEO_INDEXING G=Ctrl-Shift-F", "GLOBAL_SAVE_SUBS G=Ctrl-S", "VIDEO_DELETE_FILE V=Delete"});
    const int indexing = ids().at("GLOBAL_VIDEO_INDEXING");
    EXPECT_TRUE(map.contains(HotkeyId{indexing, GlobalHotkey}));
    EXPECT_TRUE(isRetiredHotkey(indexing));
    EXPECT_FALSE(isRetiredHotkey(ids().at("VIDEO_DELETE_FILE")));
    const auto dropped = dropRetiredHotkeys(map);
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped[0].first, (HotkeyId{indexing, GlobalHotkey}));
    EXPECT_EQ(dropped[0].second.accel, "Ctrl-Shift-F");
    EXPECT_FALSE(map.contains(HotkeyId{indexing, GlobalHotkey}));
    EXPECT_EQ(map.size(), 2u);
    EXPECT_EQ(hotkeyName(ids().at("VIDEO_DELETE_FILE")), "Unload video");
    EXPECT_EQ(hotkeyName(indexing), "");
    EXPECT_TRUE(dropRetiredHotkeys(map).empty());
    // nothing writes it back
    for (const auto &line : hotkeyLines(map, false))
        EXPECT_EQ(line.find("GLOBAL_VIDEO_INDEXING"), std::string::npos);
}

// AM1-hotkeys-window-retired: the Automation hotkeys window and its command
// are gone; a stored binding of GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW is
// recognised and dropped, and the command is never listed.
TEST(Hotkeys, RetiredAutomationHotkeysWindowBindingsAreDropped)
{
    HotkeyMap map;
    readHotkeyLines(map, {"GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW G=Ctrl-Shift-H", "GLOBAL_SAVE_SUBS G=Ctrl-S"});
    const int window = ids().at("GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW");
    EXPECT_TRUE(map.contains(HotkeyId{window, GlobalHotkey}));
    EXPECT_TRUE(isRetiredHotkey(window));
    EXPECT_FALSE(isRetiredHotkey(ids().at("GLOBAL_AUTOMATION_LOAD_SCRIPT")));
    const auto dropped = dropRetiredHotkeys(map);
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped[0].second.accel, "Ctrl-Shift-H");
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(hotkeyName(window), "");
    HotkeyMap copy;
    HotkeyList list(copy);
    list.build(defaultHotkeys());
    EXPECT_EQ(list.find("Global Open shortcut mapping window"), -1);
}

// WS3-view-arrangements-retired: stored bindings of the View arrangements
// (GLOBAL_VIEW_ALL..GLOBAL_VIEW_SUBS) and the editor switch (GLOBAL_EDITOR),
// by symbol or by number, are recognised and dropped for the notice; the
// commands next to them stay.
TEST(Hotkeys, RetiredViewAndEditorBindingsAreDropped)
{
    HotkeyMap map;
    readHotkeyLines(map, {"GLOBAL_VIEW_ALL G=Ctrl-1", "GLOBAL_VIEW_VIDEO G=Ctrl-2", "GLOBAL_VIEW_AUDIO G=Ctrl-3",
                          "5034 G=Ctrl-4", "GLOBAL_VIEW_SUBS G=Ctrl-5", "GLOBAL_EDITOR G=Ctrl-E",
                          "GLOBAL_SAVE_SUBS G=Ctrl-S"});
    std::vector<int> retired;
    for (const char *symbol : {"GLOBAL_VIEW_ALL", "GLOBAL_VIEW_AUDIO", "GLOBAL_VIEW_VIDEO", "GLOBAL_VIEW_ONLY_VIDEO",
                               "GLOBAL_VIEW_SUBS", "GLOBAL_EDITOR"}) {
        const int id = ids().at(symbol);
        EXPECT_TRUE(isRetiredHotkey(id)) << symbol;
        EXPECT_EQ(hotkeyName(id), "") << symbol;
        EXPECT_EQ(defaultHotkey(HotkeyId{id, GlobalHotkey}), "") << symbol;
        retired.push_back(id);
    }
    EXPECT_EQ(ids().at("GLOBAL_VIEW_ONLY_VIDEO"), 5034);
    // The ids beside them stay.
    EXPECT_FALSE(isRetiredHotkey(ids().at("GLOBAL_SAVE_SUBS")));
    EXPECT_FALSE(isRetiredHotkey(5030));
    EXPECT_FALSE(isRetiredHotkey(5036));
    EXPECT_FALSE(isRetiredHotkey(5108));
    EXPECT_FALSE(isRetiredHotkey(5110));
    const auto dropped = dropRetiredHotkeys(map);
    ASSERT_EQ(dropped.size(), 6u);
    for (const auto &[key, hotkey] : dropped)
        EXPECT_NE(std::find(retired.begin(), retired.end(), key.id), retired.end()) << key.id;
    EXPECT_TRUE(map.contains(HotkeyId{ids().at("GLOBAL_SAVE_SUBS"), GlobalHotkey}));
    for (const int id : retired)
        EXPECT_FALSE(map.contains(HotkeyId{id, GlobalHotkey})) << id;
    for (const auto &line : hotkeyLines(map, false)) {
        EXPECT_EQ(line.find("GLOBAL_VIEW_"), std::string::npos) << line;
        EXPECT_NE(line.rfind("GLOBAL_EDITOR ", 0), 0u) << line;
    }
}

// AV3-dummy-video-audio (owner 2026-10-18): GLOBAL_OPEN_DUMMY_AUDIO ("Open
// blank 2h30m audio") is retired the same way: a dummy video brings its own
// silent audio. A stored binding is recognised and dropped; it never had a name.
TEST(Hotkeys, RetiredBlankAudioBindingsAreDropped)
{
    HotkeyMap map;
    readHotkeyLines(map, {"GLOBAL_OPEN_DUMMY_AUDIO G=Ctrl-Alt-B", "GLOBAL_SAVE_SUBS G=Ctrl-S"});
    const int blank = ids().at("GLOBAL_OPEN_DUMMY_AUDIO");
    EXPECT_TRUE(map.contains(HotkeyId{blank, GlobalHotkey}));
    EXPECT_TRUE(isRetiredHotkey(blank));
    EXPECT_FALSE(isRetiredHotkey(ids().at("GLOBAL_OPEN_DUMMY_VIDEO")));
    const auto dropped = dropRetiredHotkeys(map);
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped[0].first, (HotkeyId{blank, GlobalHotkey}));
    EXPECT_EQ(dropped[0].second.accel, "Ctrl-Alt-B");
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(hotkeyName(blank), "");
    for (const auto &line : hotkeyLines(map, false))
        EXPECT_EQ(line.find("GLOBAL_OPEN_DUMMY_AUDIO"), std::string::npos);
    // no default binds it
    HotkeyMap defaults;
    loadDefaultHotkeys(defaults, false);
    EXPECT_FALSE(defaults.contains(HotkeyId{blank, GlobalHotkey}));
}
