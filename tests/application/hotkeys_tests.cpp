// O2: hotkeys and the Options dialog's Hotkeys page against legacy
// Hotkeys.h/.cpp, HotkeysNaming.cpp, OptionsDialog.cpp (ItemHotkey,
// AddHotkeysOnList, SetOptions, ResetDefault) and HikariListCtrl.cpp at
// 20d647c4. The action table, the names table and the defaults are read from
// the legacy sources in the repository and compared whole.

#include "hikari/application/hotkeys.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

using namespace hikari::application;

namespace {

std::string readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Hotkeys.h IDS, numbered as DECLARE_ENUM numbers them.
std::map<std::string, int> legacyIds()
{
    const std::string h = readFile(HIKARI_LEGACY_DIR "/Hotkeys.h");
    const auto begin = h.find("#define IDS(XX)");
    const auto end = h.find("DECLARE_ENUM");
    std::map<std::string, int> out;
    const std::regex xx(R"(XX\(\s*(\w+)\s*,\s*(=\s*(\d+))?\s*\))");
    int current = -1;
    const std::string body = h.substr(begin, end - begin);
    for (std::sregex_iterator it(body.begin(), body.end(), xx), last; it != last; ++it) {
        current = (*it)[3].matched ? std::stoi((*it)[3]) : current + 1;
        out[(*it)[1]] = current;
    }
    return out;
}

const std::map<std::string, int> &ids()
{
    static const auto table = legacyIds();
    return table;
}

int windowOf(const std::string &symbol)
{
    static const std::map<std::string, int> w{{"GLOBAL_HOTKEY", GlobalHotkey}, {"GRID_HOTKEY", GridHotkey},
                                              {"EDITBOX_HOTKEY", EditorHotkey}, {"VIDEO_HOTKEY", VideoHotkey},
                                              {"AUDIO_HOTKEY", AudioHotkey}};
    return w.at(symbol);
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

} // namespace

TEST(Hotkeys, ActionTableIsLegacyIdsAndTheMigrationMap)
{
    ASSERT_EQ(ids().size(), 243u);
    ASSERT_EQ(hotkeyActions().size(), 243u);
    for (const auto &a : hotkeyActions()) {
        EXPECT_EQ(ids().at(std::string(a.symbol)), a.id) << a.symbol;
        EXPECT_EQ(hotkeyIdOf(a.symbol), a.id);
        EXPECT_EQ(hotkeySymbol(a.id), a.symbol);
    }
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

TEST(Hotkeys, NamesTableIsHotkeysNaming)
{
    const std::string src = readFile(HIKARI_LEGACY_DIR "/HotkeysNaming.cpp");
    const std::regex assign(R"re(names\[(\w+)\]\s*=\s*_\("((?:[^"\\]|\\.)*)"\))re");
    std::map<int, std::string> legacy;
    for (std::sregex_iterator it(src.begin(), src.end(), assign), last; it != last; ++it)
        legacy[ids().at((*it)[1])] = (*it)[2];
    EXPECT_EQ(legacy.size(), 226u);
    EXPECT_EQ(hotkeyNames(), legacy);
    EXPECT_EQ(hotkeyName(kSave), "Save");
    EXPECT_EQ(hotkeyName(kGlobalQuit), ""); // not named: never listed
    EXPECT_EQ(hotkeyName(30100), "");
    // Two Grid actions share a name.
    EXPECT_EQ(hotkeyName(ids().at("GRID_HIDE_SELECTED")), hotkeyName(ids().at("GRID_FILTER_BY_SELECTIONS")));
}

TEST(Hotkeys, DefaultsAreLoadDefault)
{
    const std::string src = readFile(HIKARI_LEGACY_DIR "/Hotkeys.cpp");
    const std::regex assign(
        R"re(_hkeys\[idAndType\((\w+),\s*(\w+)\)\]\s*=\s*hdata\(_\("((?:[^"\\]|\\.)*)"\),\s*L"((?:[^"\\]|\\.)*)"\))re");
    HotkeyMap legacy;
    for (std::sregex_iterator it(src.begin(), src.end(), assign), last; it != last; ++it)
        legacy[HotkeyId{ids().at((*it)[1]), windowOf((*it)[2])}] = Hotkey{(*it)[3], (*it)[4]};
    EXPECT_EQ(legacy.size(), 86u);
    EXPECT_EQ(defaultHotkeys(), legacy);
    HotkeyMap main, audio;
    loadDefaultHotkeys(main, false);
    loadDefaultHotkeys(audio, true);
    EXPECT_EQ(main.size(), 63u);
    EXPECT_EQ(audio.size(), 23u);
    for (const auto &[k, h] : audio)
        EXPECT_EQ(k.type, AudioHotkey);
    // LoadDefault assigns over what is there.
    HotkeyMap loaded{{HotkeyId{kSave, GlobalHotkey}, Hotkey{"", "Ctrl-K"}}, {HotkeyId{kHistory, GridHotkey}, Hotkey{"", "F7"}}};
    loadDefaultHotkeys(loaded, false);
    EXPECT_EQ(loaded.at(HotkeyId{kSave, GlobalHotkey}).accel, "Ctrl-S");
    EXPECT_EQ(loaded.at(HotkeyId{kHistory, GridHotkey}).accel, "F7");
    // GetDefaultKey matches the window too.
    EXPECT_EQ(defaultHotkey(HotkeyId{kSave, GlobalHotkey}), "Ctrl-S");
    EXPECT_EQ(defaultHotkey(HotkeyId{kSave, GridHotkey}), "");
    EXPECT_EQ(defaultHotkey(HotkeyId{ids().at("AUDIO_COMMIT"), AudioHotkey}), "Enter");
    // Every default but GLOBAL_QUIT has a name; the windows follow GetType.
    for (const auto &[k, h] : legacy) {
        if (k.id != kGlobalQuit) {
            EXPECT_FALSE(hotkeyName(k.id).empty()) << k.id;
        }
        EXPECT_EQ(hotkeyType(k.id), k.type) << k.id;
    }
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
    EXPECT_EQ(main.size(), 63u + 2u - 1u + 1u);
    EXPECT_EQ(audio.size(), 23u);
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
    ASSERT_EQ(list.rowCount(), 226u);
    EXPECT_EQ(list.row(0).text, "Global Close current tab");
    EXPECT_EQ(list.row(0).accel, "Ctrl-W");
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
    ASSERT_EQ(more.rowCount(), 228u);
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
    EXPECT_EQ(cut.rowCount(), 225u);
    EXPECT_EQ(cut.find("Audio Commit alt"), -1);
    // A binding in a window legacy cannot name (-1) is never listed, and so
    // the list never ends early.
    auto odd = fewer;
    odd[HotkeyId{5001, -1}] = Hotkey{"", "F1"};
    HotkeyList whole(copy);
    whole.build(odd);
    EXPECT_EQ(whole.rowCount(), 226u);
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
    EXPECT_EQ(count(1), 85u); // set shortcuts (GLOBAL_QUIT is not listed)
    EXPECT_EQ(count(3), std::size_t(std::ranges::count_if(hotkeyNames(), [](const auto &n) { return hotkeyType(n.first) == GridHotkey; })));
    EXPECT_EQ(count(6), 23u);
    list.filter(5);
    EXPECT_EQ(list.row(list.selection()).key.type, VideoHotkey); // the first shown
    list.selectShown(2);
    EXPECT_EQ(list.selection(), list.shown()[2]);
    // A row deleted under "Set shortcuts" stays shown until filtered again.
    list.filter(1);
    const int first = list.selection();
    list.remove(first, live);
    EXPECT_EQ(list.shown().size(), 85u);
    EXPECT_EQ(list.row(first).accel, "");
    list.filter(1);
    EXPECT_EQ(list.shown().size(), 84u);
    list.filter(0);
    EXPECT_EQ(list.shown().size(), 226u);
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
        auto c = list.mapConflict(stop, "H", VideoHotkey, live); // Audio Stop holds H
        ASSERT_TRUE(c);
        EXPECT_TRUE(c->doubled);
        EXPECT_EQ(c->message, "This hotkey already exists for \"Stop\".\nWhat to do?");
        list.map(stop, "H", VideoHotkey, HotkeyAnswer::Delete, live);
        EXPECT_EQ(list.row(stop).accel, "H");
        EXPECT_EQ(rowNamed(list, "Audio Stop").accel, "H"); // another window: untouched
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
    ASSERT_EQ(list.rowCount(), 227u);
    const auto &added = list.row(226);
    EXPECT_EQ(added.text, "Video Save");
    EXPECT_EQ((added.key), (HotkeyId{kSave, VideoHotkey}));
    EXPECT_EQ(added.accel, "F7");
    EXPECT_EQ(list.selection(), -1); // SetSelection(GetCount()) selects nothing
    EXPECT_EQ(copy.at(HotkeyId{kSave, VideoHotkey}).accel, "F7");
    // Again: that row changes and is selected.
    list.map(save, "F6", VideoHotkey, HotkeyAnswer::Cancel, live);
    EXPECT_EQ(list.rowCount(), 227u);
    EXPECT_EQ(list.row(226).accel, "F6");
    EXPECT_EQ(list.selection(), 226);
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
    EXPECT_EQ(third.shown().size(), 226u); // every row shown, the filter choice as it was
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
    // Only the same window is "the same" here (no Video/Audio rule).
    c = mapHotkeyNowConflict(live, ids().at("VIDEO_STOP"), "H", VideoHotkey);
    ASSERT_TRUE(c);
    EXPECT_FALSE(c->doubled);
    EXPECT_EQ(c->message, "This shortcut already exists in another window as a shortcut for \"Audio Stop\".\nWhat "
                          "would you like to do?");
    auto r = mapHotkeyNow(live, ids().at("VIDEO_STOP"), "Stop", "H", VideoHotkey, HotkeyAnswer::SetAnyway);
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.saveAudio); // an Audio binding was named
    EXPECT_EQ(live.at(HotkeyId{ids().at("VIDEO_STOP"), VideoHotkey}).accel, "H");
    EXPECT_EQ(live.at(HotkeyId{ids().at("AUDIO_STOP"), AudioHotkey}).accel, "H");
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
