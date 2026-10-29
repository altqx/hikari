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

constexpr int kSave = 5000;
constexpr int kHistory = 5008;
constexpr int kDuplicate = 4527;
constexpr int kNextDoubtful = 3017;

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
        {2033, "VIDEO_TOOL_PERSPECTIVE", "Perspective", "Q"},
};

}

TEST(Hotkeys, ActionTableIsLegacyIdsAndTheMigrationMap)
{
    ASSERT_EQ(ids().size(), 243u);
    ASSERT_EQ(hotkeyActions().size(), 278u);
    for (const auto &a : hotkeyActions().first(kLegacyHotkeyActions)) {
        EXPECT_EQ(hotkeyIdOf(a.symbol), a.id);
        EXPECT_EQ(hotkeySymbol(a.id), a.symbol);
    }
    for (const auto &a : hotkeyActions().subspan(kLegacyHotkeyActions)) {
        EXPECT_FALSE(ids().contains(std::string(a.symbol))) << a.symbol;
        EXPECT_TRUE(std::ranges::none_of(ids(), [&](const auto &l) { return l.second == a.id; })) << a.symbol;
        const bool editor = std::string_view(a.symbol).starts_with("EDITBOX_");
        const bool audio = a.id >= 1028 && a.id <= 1032;
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
    EXPECT_EQ(hotkeyIdOf("NOT_A_SYMBOL"), 0);
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
    EXPECT_EQ(hotkeyType(3119), EditorHotkey);
    EXPECT_EQ(hotkeyType(4001), GridHotkey);
    EXPECT_EQ(hotkeyType(4556), GridHotkey);
    EXPECT_EQ(hotkeyType(5000), GlobalHotkey);
    EXPECT_EQ(hotkeyType(30100), GlobalHotkey);
    EXPECT_EQ(hotkeyWindowName(GridHotkey), "Subtitles");
}

TEST(Hotkeys, StoredLinesAreSaveHkeys)
{
    HotkeyMap map = defaultHotkeys();
    map[HotkeyId{kSave, GridHotkey}] = Hotkey{"Save", "F7"};
    map[HotkeyId{kHistory, GlobalHotkey}].accel.clear();
    map[HotkeyId{30100, GlobalHotkey}] = Hotkey{"Script a.lua-1", "Ctrl-K"};
    map[HotkeyId{7000, VideoHotkey}] = Hotkey{"", "F12"};
    map[HotkeyId{ids().at("AUDIO_PLAY"), GlobalHotkey}] = Hotkey{"", "Ctrl-P"};
    const auto main = hotkeyLines(map, false);
    const auto audio = hotkeyLines(map, true);
    EXPECT_EQ(main.size(), 62u + 2u - 1u + 1u + 12u + 9u);
    EXPECT_EQ(audio.size(), 23u + 3u + 2u);
    EXPECT_EQ(main.front(), "AUDIO_PLAY G=Ctrl-P");
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
    EXPECT_EQ(qtKeysOfAccel("Num -"), "-");
    EXPECT_EQ(qtKeysOfAccel("Shift-Ctrl-X"), "Ctrl+Shift+X");
    EXPECT_EQ(qtKeysOfAccel("Ctrl-Bogus"), "");
    EXPECT_EQ(qtKeysOfAccel("Num |"), "");
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
    EXPECT_EQ(captureHotkey(press(wxk::Shift, false, false, true), GlobalHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press(wxk::Control, false, true, false), GlobalHotkey).result, R::Ignored);
    auto c = captureHotkey(press('K', true, true, true), GlobalHotkey);
    EXPECT_EQ(c.result, R::Accepted);
    EXPECT_EQ(c.accel, "Alt-Ctrl-Shift-K");
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
    c = captureHotkey(press('V', false, true, false), VideoHotkey);
    EXPECT_EQ(c.result, R::Refused);
    EXPECT_EQ(c.message, "You cannot use shortcuts for copying, cutting, and pasting.");
    EXPECT_EQ(captureHotkey(press('Z', false, true, false), GridHotkey).result, R::Refused);
    EXPECT_EQ(captureHotkey(press('V', false, true, true), GridHotkey).accel, "Ctrl-Shift-V");
    c = captureHotkey(press(wxk::F4, true, false, false), VideoHotkey);
    EXPECT_EQ(c.message, "You cannot use the program exit shortcut.");
    EXPECT_EQ(captureHotkey(press(wxk::F4, true, true, false), VideoHotkey).accel, "Alt-Ctrl-F4");
    EXPECT_EQ(captureHotkey(press(wxk::Escape, false, false, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press('#', false, true, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press('a', false, true, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press(0x0105, false, true, false), VideoHotkey).result, R::Ignored);
    EXPECT_EQ(captureHotkey(press(wxk::Home + 62, false, true, false), VideoHotkey).result, R::Ignored);
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
    EXPECT_TRUE(textFieldOwnsKey("Num 1", false));
    EXPECT_FALSE(textFieldOwnsKey("Ctrl-Num 1", false));
}

TEST(Hotkeys, ListOrderIsAddHotkeysOnList)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    auto live = defaultHotkeys();
    list.build(live);
    ASSERT_EQ(list.rowCount(), 254u);
    EXPECT_EQ(list.row(0).text, "Global Close current tab");
    EXPECT_EQ(list.row(0).accel, "Ctrl-W");
    EXPECT_EQ(rowNamed(list, "Global Hide tags in the line editor").accel, "");
    EXPECT_EQ(rowNamed(list, "Global Show / hide render cost").accel, "");
    EXPECT_EQ(rowNamed(list, "Video Full screen bar: Auto / Always / Never").accel, "Tab");
    EXPECT_EQ(rowNamed(list, "Audio Tap to time").accel, "Y");
    for (const auto &tool : kToolCommands)
        EXPECT_EQ(rowNamed(list, std::string("Video ") + tool.name).accel, tool.accel);
    EXPECT_EQ(rowNamed(list, "Global Check karaoke").accel, "");
    EXPECT_EQ(rowNamed(list, "Global Kanji timer").accel, "");
    EXPECT_EQ(rowNamed(list, "Global Getting started").accel, "");
    EXPECT_EQ(rowNamed(list, "Subtitles Hide line wraps").key.id, 4513);
    EXPECT_EQ(list.row(int(list.rowCount()) - 1).text, "Audio Commit alt");
    EXPECT_EQ(list.find("Global Exit"), -1);
    EXPECT_EQ(list.selection(), 0);
    EXPECT_FALSE(list.modified());
    int last = -1;
    for (std::size_t i = 0; i < list.rowCount(); ++i) {
        const auto &r = list.row(int(i));
        EXPECT_GE(r.key.type, last);
        last = r.key.type;
        EXPECT_EQ(r.text, std::string(hotkeyWindowName(r.key.type)) + " " + r.name);
    }
    EXPECT_EQ(rowNamed(list, "Video Volume down").accel, "Num 0");
    EXPECT_EQ(rowNamed(list, "Editor First tag button").accel, "");

    live[HotkeyId{30100, GlobalHotkey}] = Hotkey{"Script a.lua-1", "Ctrl-K"};
    live[HotkeyId{kSave, GridHotkey}] = Hotkey{"Save", "F7"};
    HotkeyList more(copy);
    more.build(live);
    ASSERT_EQ(more.rowCount(), 256u);
    const int script = more.find("Global Script a.lua-1");
    const int copied = more.find("Subtitles Save");
    ASSERT_GE(script, 0);
    ASSERT_GE(copied, 0);
    EXPECT_EQ(more.row(script - 1).key.type, GlobalHotkey);
    EXPECT_EQ(more.row(script + 1).key.type, GridHotkey);
    EXPECT_EQ(more.row(copied + 1).key.type, EditorHotkey);
    EXPECT_EQ(more.row(copied).accel, "F7");

    auto fewer = defaultHotkeys();
    fewer.erase(HotkeyId{kAudioCommitAlt, AudioHotkey});
    HotkeyList cut(copy);
    cut.build(fewer);
    EXPECT_EQ(cut.rowCount(), 253u);
    EXPECT_EQ(cut.find("Audio Commit alt"), -1);
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
    EXPECT_EQ(count(1), 110u);
    EXPECT_EQ(count(3), std::size_t(std::ranges::count_if(hotkeyNames(), [](const auto &n) { return hotkeyType(n.first) == GridHotkey; })));
    EXPECT_EQ(count(6), 28u);
    list.filter(5);
    EXPECT_EQ(list.row(list.selection()).key.type, VideoHotkey);
    list.selectShown(2);
    EXPECT_EQ(list.selection(), list.shown()[2]);
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

TEST(Hotkeys, SearchNarrowsTheFilter)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    const auto live = defaultHotkeys();
    list.build(live);
    list.search("gLOBAL hISTORY");
    ASSERT_EQ(list.shown().size(), 1u);
    EXPECT_EQ(list.row(list.selection()).text, "Global History");
    list.search("ctrl-shift-h");
    EXPECT_TRUE(std::ranges::any_of(list.shown(), [&](int i) { return list.row(i).text == "Global History"; }));
    list.filter(5);
    EXPECT_TRUE(std::ranges::none_of(list.shown(), [&](int i) { return list.row(i).text == "Global History"; }));
    list.filter(0);
    list.search("no such function");
    EXPECT_TRUE(list.shown().empty());
    list.search("my script", [](const HotkeyList::Row &r) { return r.text == "Global Save" ? "My Script" : ""; });
    ASSERT_EQ(list.shown().size(), 1u);
    EXPECT_EQ(list.row(list.shown()[0]).text, "Global Save");
    list.search("");
    EXPECT_EQ(list.shown().size(), list.rowCount());
}

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
    list.filter(5);
    list.resetAll(live);
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    EXPECT_EQ(copy.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-H");
    EXPECT_EQ(list.row(history).visible, 0);
    list.undo();
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-J");
    EXPECT_EQ(list.row(open).accel, "");
    list.redo();
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
}

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
    EXPECT_TRUE(shown(history));
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
    EXPECT_EQ(copy, live);
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
    EXPECT_FALSE(list.commit());
}

TEST(Hotkeys, ConflictsPerWindow)
{
    const auto live = defaultHotkeys();
    {
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(live);
        const int history = list.find("Global History");
        auto c = list.mapConflict(history, "Ctrl-S", GlobalHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        EXPECT_EQ(c->message, "This hotkey already exists for \"Save\".\nWhat to do?");
        EXPECT_EQ(c->commands, std::vector<std::string>{"Save"});
        EXPECT_TRUE(c->canSwitch);
        EXPECT_FALSE(c->canSetAnyway);
        list.map(history, "Ctrl-S", GlobalHotkey, HotkeyAnswer::Cancel, live);
        EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
        list.map(history, "Ctrl-S", GlobalHotkey, HotkeyAnswer::Switch, live);
        EXPECT_EQ(list.row(history).accel, "Ctrl-S");
        EXPECT_EQ(rowNamed(list, "Global Save").accel, "Ctrl-Shift-H");
        EXPECT_EQ(copy.at(HotkeyId{kSave, GlobalHotkey}).accel, "Ctrl-Shift-H");
        const int open = list.find("Global Open subtitles");
        list.map(open, "Ctrl-S", GlobalHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(history).accel, "");
        EXPECT_EQ(copy.at(HotkeyId{kHistory, GlobalHotkey}).accel, "");
        EXPECT_EQ(list.row(open).accel, "Ctrl-S");
    }
    {
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(live);
        const int dup = list.find("Subtitles Duplicate lines");
        auto c = list.mapConflict(dup, "Ctrl-R", GridHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_FALSE(c->doubled);
        EXPECT_EQ(c->message, "This shortcut already exists in another window as a shortcut for \"Editor Next "
                              "untranslated line\".\nWhat would you like to do?");
        EXPECT_TRUE(c->canSwitch);
        EXPECT_TRUE(c->canSetAnyway);
        list.map(dup, "Ctrl-R", GridHotkey, HotkeyAnswer::SetAnyway, live);
        EXPECT_EQ(list.row(dup).accel, "Ctrl-R");
        EXPECT_EQ(rowNamed(list, "Editor Next untranslated line").accel, "Ctrl-R");
        copy[HotkeyId{kSave, VideoHotkey}] = Hotkey{"Save", "Ctrl-R"};
        const int history = list.find("Global History");
        c = list.mapConflict(history, "Ctrl-R", GlobalHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_EQ(c->count, 3u);
        EXPECT_FALSE(c->canSwitch);
        EXPECT_EQ(c->message, "This shortcut already exists in other windows as a shortcut for \"Subtitles Duplicate "
                              "lines, Editor Next untranslated line, Video Save\".\nWhat would you like to do?");
        EXPECT_EQ(c->commands, (std::vector<std::string>{"Duplicate lines", "Next untranslated line", "Save"}));
        list.map(history, "Ctrl-R", GlobalHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(dup).accel, "");
        EXPECT_EQ(rowNamed(list, "Editor Next untranslated line").accel, "");
        EXPECT_EQ(copy.at(HotkeyId{kSave, VideoHotkey}).accel, "Ctrl-R");
    }
    {
        HotkeyMap copy;
        HotkeyList list(copy);
        list.build(live);
        const int stop = list.find("Video Stop");
        auto c = list.mapConflict(stop, "W", VideoHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        EXPECT_EQ(c->message, "This hotkey already exists for \"Play 500ms after\".\nWhat to do?");
        list.map(stop, "W", VideoHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(stop).accel, "W");
        EXPECT_EQ(rowNamed(list, "Audio Play 500ms after").accel, "W");
        const int insert = list.find("Subtitles Insert before");
        c = list.mapConflict(insert, "L", GridHotkey, live);
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        const int newTab = list.find("Global Open new tab");
        c = list.mapConflict(newTab, "Shift-L", GlobalHotkey, live);
        EXPECT_FALSE(c);
    }
    {
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
        EXPECT_EQ(copy.at(HotkeyId{30100, GlobalHotkey}).accel, "Ctrl-K");
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
    EXPECT_EQ(list.row(save).accel, "Ctrl-S");
    ASSERT_EQ(list.rowCount(), 255u);
    const auto &added = list.row(254);
    EXPECT_EQ(added.text, "Video Save");
    EXPECT_EQ((added.key), (HotkeyId{kSave, VideoHotkey}));
    EXPECT_EQ(added.accel, "F7");
    EXPECT_EQ(list.selection(), -1);
    EXPECT_EQ(copy.at(HotkeyId{kSave, VideoHotkey}).accel, "F7");
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
    list.map(open, "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Switch, live);
    EXPECT_EQ(list.row(history).accel, "Ctrl-O");
    list.reset(history, live);
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    list.reset(open, live);
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
    list.remove(open, live);
    EXPECT_EQ(list.row(open).accel, "");
    EXPECT_TRUE(list.row(open).keyModified);
    EXPECT_EQ(copy.at(HotkeyId{5100, GlobalHotkey}).accel, "");
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
    list.undo();
    EXPECT_EQ(list.row(history).accel, "Ctrl-Shift-H");
    list.remove(history, live);
    list.redo();
    EXPECT_EQ(list.row(history).accel, "");
    EXPECT_EQ(list.row(open).accel, "Ctrl-O");
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
    HotkeyMap copy;
    auto live = defaultHotkeys();
    {
        HotkeyList first(copy);
        first.build(live);
        first.map(first.find("Global History"), "Ctrl-Shift-J", GlobalHotkey, HotkeyAnswer::Cancel, live);
    }
    mapHotkeyNow(live, kDuplicate, "Duplicate lines", "Ctrl-Shift-D", GridHotkey, HotkeyAnswer::Cancel);
    HotkeyList second(copy);
    second.build(live);
    EXPECT_EQ(rowNamed(second, "Global History").accel, "Ctrl-Shift-H");
    EXPECT_EQ(rowNamed(second, "Subtitles Duplicate lines").accel, "Ctrl-Shift-D");
    second.remove(second.find("Global Save"), live);
    const auto committed = second.commit();
    ASSERT_TRUE(committed);
    EXPECT_EQ(committed->at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-J");
    EXPECT_EQ(committed->at(HotkeyId{kDuplicate, GridHotkey}).accel, "Ctrl-D");
    EXPECT_EQ(committed->at(HotkeyId{kSave, GlobalHotkey}).accel, "");

    HotkeyList third(copy);
    third.build(*committed);
    third.map(third.find("Global Open subtitles"), "F7", GlobalHotkey, HotkeyAnswer::Cancel, *committed);
    third.filter(2);
    auto reset = defaultHotkeys();
    third.clear();
    third.build(reset);
    EXPECT_EQ(third.shown().size(), 254u);
    EXPECT_EQ(third.filterMode(), 2);
    EXPECT_EQ(rowNamed(third, "Global Open subtitles").accel, "Ctrl-O");
    EXPECT_TRUE(third.modified());
    third.undo();
    EXPECT_EQ(rowNamed(third, "Global Open subtitles").accel, "Ctrl-O");
    third.remove(third.find("Global Find"), reset);
    third.undo();
    EXPECT_EQ(rowNamed(third, "Global Find").accel, "Ctrl-F");
    const auto after = third.commit();
    ASSERT_TRUE(after);
    EXPECT_EQ(after->at(HotkeyId{5100, GlobalHotkey}).accel, "F7");
    EXPECT_EQ(after->at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-Shift-J");
}

TEST(Hotkeys, GestureMapsAtOnce)
{
    auto live = defaultHotkeys();
    auto c = mapHotkeyNowConflict(live, kSave, "Ctrl-S", GlobalHotkey);
    ASSERT_TRUE(c);
    EXPECT_TRUE(c->doubled);
    EXPECT_EQ(c->message, "This hotkey already exists for \"Save\".\nWhat to do?");
    c = mapHotkeyNowConflict(live, ids().at("VIDEO_STOP"), "W", VideoHotkey);
    ASSERT_TRUE(c);
    EXPECT_FALSE(c->doubled);
    EXPECT_EQ(c->message, "This shortcut already exists in another window as a shortcut for \"Audio Play 500ms "
                          "after\".\nWhat would you like to do?");
    auto r = mapHotkeyNow(live, ids().at("VIDEO_STOP"), "Stop", "W", VideoHotkey, HotkeyAnswer::SetAnyway);
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.saveAudio);
    EXPECT_EQ(live.at(HotkeyId{ids().at("VIDEO_STOP"), VideoHotkey}).accel, "W");
    EXPECT_EQ(live.at(HotkeyId{ids().at("AUDIO_PLAY_500MS_AFTER"), AudioHotkey}).accel, "W");
    r = mapHotkeyNow(live, kNextDoubtful, "Next unconfirmed line", "Ctrl-R", EditorHotkey, HotkeyAnswer::Switch);
    EXPECT_FALSE(r.saveAudio);
    EXPECT_EQ(live.at(HotkeyId{kNextDoubtful, EditorHotkey}).accel, "Ctrl-R");
    EXPECT_EQ(live.at(HotkeyId{3018, EditorHotkey}).accel, "Ctrl-D");
    r = mapHotkeyNow(live, kSave, "Save", "Ctrl-D", GridHotkey, HotkeyAnswer::Delete);
    EXPECT_EQ(live.at(HotkeyId{kDuplicate, GridHotkey}).accel, "");
    EXPECT_EQ(live.at(HotkeyId{3018, EditorHotkey}).accel, "Ctrl-D");
    EXPECT_EQ(live.at(HotkeyId{kSave, GridHotkey}).name, "Save");
    const auto before = live;
    r = mapHotkeyNow(live, kHistory, "History", "Ctrl-S", GlobalHotkey, HotkeyAnswer::Cancel);
    EXPECT_FALSE(r.changed);
    EXPECT_EQ(live, before);
    r = mapHotkeyNow(live, ids().at("AUDIO_GOTO"), "Go to selection", "F12", AudioHotkey, HotkeyAnswer::Cancel);
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.saveAudio);
}

TEST(Hotkeys, GestureSwitchFollowsLegacyOrder)
{
    HotkeyMap live;
    live[HotkeyId{5005, GlobalHotkey}] = Hotkey{"", "Ctrl-K"};
    live[HotkeyId{kHistory, GlobalHotkey}] = Hotkey{"", "Ctrl-K"};
    live[HotkeyId{5100, GlobalHotkey}] = Hotkey{"", "Ctrl-K"};
    live[HotkeyId{kDuplicate, GridHotkey}] = Hotkey{"", "Ctrl-K"};
    const auto c = mapHotkeyNowConflict(live, kHistory, "Ctrl-K", GlobalHotkey);
    ASSERT_TRUE(c);
    EXPECT_TRUE(c->doubled);
    const auto r = mapHotkeyNow(live, kHistory, "History", "Ctrl-K", GlobalHotkey, HotkeyAnswer::Switch);
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(live.at(HotkeyId{5005, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{5100, GlobalHotkey}).accel, "");
    EXPECT_EQ(live.at(HotkeyId{kDuplicate, GridHotkey}).accel, "Ctrl-K");
    live[HotkeyId{5100, GlobalHotkey}].accel = "Ctrl-J";
    live[HotkeyId{5005, GlobalHotkey}].accel = "Ctrl-J";
    mapHotkeyNow(live, kHistory, "History", "Ctrl-J", GlobalHotkey, HotkeyAnswer::Switch);
    EXPECT_EQ(live.at(HotkeyId{5005, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{5100, GlobalHotkey}).accel, "Ctrl-K");
    EXPECT_EQ(live.at(HotkeyId{kHistory, GlobalHotkey}).accel, "Ctrl-J");
}

TEST(Hotkeys, InvalidKeysAreReportedAsGetHKeyLogsThem)
{
    EXPECT_EQ(invalidHotkeyKey("Ctrl-Bogus"), "Bogus");
    EXPECT_EQ(invalidHotkeyKey("Ctrl-Shift"), "Shift");
    EXPECT_EQ(invalidHotkeyKey("Num 0"), "");
    EXPECT_EQ(invalidHotkeyKey("Num -"), "");
    EXPECT_EQ(invalidHotkeyKey("Ctrl-A"), "");
    EXPECT_EQ(invalidHotkeyKey("Alt-\xc3\xa9"), "");
    EXPECT_EQ(qtKeysOfAccel("Alt-\xc3\xa9"), "");
    EXPECT_EQ(invalidHotkeyKey("Alt-\xf0\x9f\x98\x80"), "\xf0\x9f\x98\x80");
    EXPECT_EQ(invalidHotkeyKey(""), "");
}

TEST(Hotkeys, NumericLabelsAreReadWithEachPlatformsWxAtoi)
{
    EXPECT_EQ(hotkeyLabelNumber("99999999999", true), 2147483647);
    EXPECT_EQ(hotkeyLabelNumber("99999999999", false), 1215752191);
    EXPECT_EQ(hotkeyLabelNumber("-99999999999", true), -2147483647 - 1);
    EXPECT_EQ(hotkeyLabelNumber("-99999999999", false), -1215752191);
    EXPECT_EQ(hotkeyLabelNumber("4294971297", true), 2147483647);
    EXPECT_EQ(hotkeyLabelNumber("4294971297", false), 4001);
    EXPECT_EQ(hotkeyLabelNumber("99999999999999999999999", true), 2147483647);
    EXPECT_EQ(hotkeyLabelNumber("99999999999999999999999", false), -1);
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
        EXPECT_EQ(map.at(HotkeyId{0, GlobalHotkey}).accel, "F4");
        std::vector<std::string> expected{"GRID_DUPLICATE_LINES S=F3"};
        if (!windows)
            expected.insert(expected.begin(), "GRID_HIDE_LAYER S=F1");
        EXPECT_EQ(hotkeyLines(map, false), expected);
    }
    HotkeyMap host;
    readHotkeyLines(host, {"99999999999 G=Ctrl-K"});
#ifdef _WIN32
    EXPECT_TRUE(host.contains(HotkeyId{2147483647, GlobalHotkey}));
#else
    EXPECT_TRUE(host.contains(HotkeyId{1215752191, GlobalHotkey}));
#endif
}

TEST(Hotkeys, FilteredListShowsAndHidesBlocks)
{
    HotkeyMap copy;
    HotkeyList list(copy);
    list.build(defaultHotkeys());
    EXPECT_FALSE(list.filtered());
    list.filter(1);
    ASSERT_TRUE(list.filtered());
    const auto shown = list.shown();
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
    list.toggleBlock(int(shown.size()) - 1);
    EXPECT_EQ(list.shown(), shown);
    list.filter(0);
    EXPECT_FALSE(list.filtered());
    EXPECT_EQ(list.hiddenBlock(0), 0);
}

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
    for (const auto &line : hotkeyLines(map, false))
        EXPECT_EQ(line.find("GLOBAL_VIDEO_INDEXING"), std::string::npos);
}

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
    HotkeyMap defaults;
    loadDefaultHotkeys(defaults, false);
    EXPECT_FALSE(defaults.contains(HotkeyId{blank, GlobalHotkey}));
}
