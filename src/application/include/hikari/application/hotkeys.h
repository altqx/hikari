#pragma once

// O2: legacy hotkeys (Hotkeys.h/.cpp, HotkeysNaming.cpp and the Options
// dialog's Hotkeys page in OptionsDialog.cpp at 20d647c4) without Qt.
// - The action table: every IDS symbol with its numeric id, the names table
//   the Options list shows (HotkeysNaming), GetType and the defaults
//   (LoadDefault for the main and the audio file).
// - A binding is keyed by (id, window): the window it was mapped for, which
//   may differ from the id's own (GetType). Bindings are legacy accelerator
//   text ("Ctrl-Shift-O", "Num 0", "Enter").
// - The stored form is legacy SaveHkeys' lines ("GLOBAL_SAVE_SUBS G=Ctrl-S"),
//   one list for Hotkeys.txt's bindings and one for AudioHotkeys.txt's; the
//   script bindings (ids from 30100) are the automation hotkeys (S2).
// - HkeysDialog::OnKeyPress (the "Hotkey mapping" window) turns a key press
//   into accelerator text or one of its refusals.
// - HotkeyList is the Options page: its list (AddHotkeysOnList), filter,
//   selection and undo history (HikariListCtrl), and Map hotkey, Restore
//   default hotkey and Delete hotkey (ItemHotkey) over legacy's static
//   hotkeysCopy, which outlives the dialog.
// - mapHotkeyNow is Hotkeys::OnMapHkey, the Shift+click gesture on a menu
//   item or a mapped button: it changes the live bindings at once.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// The window a binding belongs to (legacy GLOBAL_HOTKEY ... AUDIO_HOTKEY).
enum HotkeyWindow : int { GlobalHotkey = 0, GridHotkey, EditorHotkey, VideoHotkey, AudioHotkey };
inline constexpr int kHotkeyWindows = 5;

struct HotkeyId {
    int id = 0;
    int type = GlobalHotkey;
    friend bool operator==(const HotkeyId &, const HotkeyId &) = default;
    // Legacy operator<: by window, then id.
    friend bool operator<(const HotkeyId &a, const HotkeyId &b)
    {
        return a.type != b.type ? a.type < b.type : a.id < b.id;
    }
};

// Legacy hdata.
struct Hotkey {
    std::string name;
    std::string accel;
    friend bool operator==(const Hotkey &, const Hotkey &) = default;
};
using HotkeyMap = std::map<HotkeyId, Hotkey>;

// Ids the legacy logic names.
inline constexpr int kAudioCommitAlt = 1000;
inline constexpr int kAudioCommit = 1010;
inline constexpr int kAudioNext = 1014;
inline constexpr int kVideoPlayPause = 2000;
inline constexpr int kVideo5SecondsBackward = 2003;
inline constexpr int kEditorFirst = 3000;
inline constexpr int kEditorTagButton1 = 3100;
inline constexpr int kEditorTagButton20 = 3119;
inline constexpr int kGridFirst = 4001;
inline constexpr int kGridFilterIgnoreInActions = 4546;
inline constexpr int kGlobalFirst = 5000;
inline constexpr int kGlobalQuit = 5108;
inline constexpr int kFirstScriptHotkey = 30100;

struct HotkeyAction {
    std::string_view symbol;
    int id;
};
// IDS in declaration order (243 symbols).
std::span<const HotkeyAction> hotkeyActions();
// GetIdValue: 0 for an unknown symbol.
int hotkeyIdOf(std::string_view symbol);
// GetString: "" for an id without a symbol.
std::string_view hotkeySymbol(int id);
// HotkeysNaming (English): the names the Options list shows, by id.
const std::map<int, std::string> &hotkeyNames();
// HotkeysNaming::GetName: "" when the id has no name (scripts, GLOBAL_QUIT...).
std::string hotkeyName(int id);
// Hotkeys::GetType: the window an id's range belongs to.
int hotkeyType(int id);
// The window names the Options list prefixes ("Global", "Subtitles",
// "Editor", "Video", "Audio").
std::string_view hotkeyWindowName(int type);

// LoadDefault: assigns the defaults of the main (or audio) file over `map`.
void loadDefaultHotkeys(HotkeyMap &map, bool audio);
// Hotkeys::ResetDefaults: both defaults and nothing else.
HotkeyMap defaultHotkeys();
// GetDefaultKey: the default accelerator of exactly (id, window), or "".
std::string defaultHotkey(const HotkeyId &key);

// SaveHkeys' lines for the main (or audio) file, without its "[version]"
// header: AUDIO_HOTKEY bindings go to the audio file, the others to the main
// one; empty bindings, ids under 100 and script ids are not written (the
// script bindings are the automation hotkeys').
std::vector<std::string> hotkeyLines(const HotkeyMap &map, bool audio);
// LoadHkeys' reading of those lines into `map`: "<SYMBOL or id> <G|S|E|V|A>=<accel>";
// a symbol it does not know becomes id 0 (GetIdValue), a window letter it
// does not know window -1, and an empty binding is skipped. Script lines
// ("Script ...") belong to the automation hotkeys and are skipped. The
// version header and the record count rule are the importer's (C04).
void readHotkeyLines(HotkeyMap &map, const std::vector<std::string> &lines);

// Hotkeys::GetHKey's reading of accelerator text: the modifiers found
// anywhere in it ("Alt-", "Ctrl-", "Shift-") and the key after the last '-'
// (or '-' itself when it ends with one), named by FillTable or one character.
// Returns the key as a Qt portable sequence ("Ctrl+Shift+O", "Num+0",
// "Return"), or "" when the key is invalid (legacy logs "Shortcut ... is
// invalid" and installs nothing).
std::string qtKeysOfAccel(std::string_view accel);
// The other way, for bindings captured as Qt portable sequences (the
// automation hotkeys'): legacy names and modifier order ("Alt-Ctrl-Shift-").
std::string accelOfQtKeys(std::string_view keys);

// wx key codes HkeysDialog::OnKeyPress reads (wxWidgets defs.h).
namespace wxk {
inline constexpr int Back = 8, Tab = 9, Return = 13, Escape = 27, Space = 32, Delete = 127;
inline constexpr int Shift = 306, Alt = 307, Control = 308, Pause = 310, End = 312, Home = 313, Left = 314, Up = 315,
                     Right = 316, Down = 317, Insert = 322, Numpad0 = 324, F1 = 340, F4 = 343, F24 = 363,
                     PageUp = 366, PageDown = 367, NumpadEnter = 370, NumpadHome = 375, NumpadDelete = 385,
                     NumpadMultiply = 387, NumpadAdd = 388, NumpadSeparator = 389, NumpadSubtract = 390,
                     NumpadDecimal = 391, NumpadDivide = 392, WindowsLeft = 393;
} // namespace wxk

// FillTable's key names, by wx key code.
const std::map<int, std::string> &hotkeyKeyNames();

struct HotkeyPress {
    int key = 0; // wx key code (GetKeyCode, else GetUnicodeKey)
    bool alt = false;
    bool ctrl = false;
    bool shift = false;
    bool onlyCtrl = false; // GetModifiers() == wxMOD_CONTROL
};
struct HotkeyCapture {
    enum class Result { Ignored, Refused, Accepted };
    Result result = Result::Ignored;
    std::string accel;   // Accepted: the binding
    std::string message; // Refused: the message box shown; the window stays open
};
// HkeysDialog::OnKeyPress for the window chosen (`type`).
HotkeyCapture captureHotkey(const HotkeyPress &press, int type);

// The accelerators legacy's text editor (DialogueTextEditor) installs for
// itself: they act in the Line editor's text fields whatever a binding says
// (numpad digits only when TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS is off).
bool textFieldOwnsKey(std::string_view accel, bool allowNumpadHotkeys);

// A "This hotkey already exists" / "This shortcut already exists in ..."
// question (HikariMessageDialog) before a binding is set.
struct HotkeyConflict {
    bool doubled = false;     // the same window (or a window legacy treats as the same)
    std::size_t count = 0;    // bindings holding the keys
    std::string message;
    bool canSwitch = false;   // "Switch hotkeys" (wxOK)
    bool canSetAnyway = false; // "Set anyway" (wxNO)
};
enum class HotkeyAnswer { Switch, Delete, SetAnyway, Cancel };

// The Options dialog's Hotkeys page. `copy` is legacy's static
// OptionsDialog::hotkeysCopy: it is filled from the live bindings at the
// first edit while it is empty and is never cleared, so it outlives the
// dialog (and "Set default").
class HotkeyList {
public:
    struct Row {
        std::string text;  // the Function column: "<window> <name>"
        bool textModified = false;
        std::string name;  // ItemHotkey::name
        std::string accel; // the Hotkey column
        bool keyModified = false;
        HotkeyId key;
        bool visible = true; // ItemRow::isVisible
    };

    explicit HotkeyList(HotkeyMap &copy) : m_copy(copy) {}
    HotkeyList(const HotkeyList &) = delete;
    HotkeyList &operator=(const HotkeyList &) = delete;

    // AddHotkeysOnList, StartEdition and SetSelection(0). Clearing first is
    // ClearList (the list's own history and modified flag are not reset).
    void build(const HotkeyMap &live);
    void clear();

    std::size_t rowCount() const { return m_rows.size(); }
    const Row &row(int index) const { return *m_rows[std::size_t(index)]; }
    // The rows shown, in order (indexes into rows()).
    std::vector<int> shown() const;
    bool modified() const { return m_modified; }
    int filterMode() const { return m_filterMode; }
    // FilterList(1, mode): 0 All, 1 Set shortcuts, 2 Global, 3 Subtitles,
    // 4 Editor, 5 Video, 6 Audio. The selection goes to the first shown row.
    void filter(int mode);
    // GetSelection: the selected row, or -1.
    int selection() const;
    // SetSelection of a shown position.
    void selectShown(int position);
    // The row whose Function column reads `text` (FindItem), or -1.
    int find(const std::string &text) const;

    // The "Hotkey mapping" window's title name and whether it offers the
    // window choice (not for scripts).
    std::string mapName(int index) const { return row(index).name; }
    bool mapOffersWindows(int index) const { return !row(index).name.starts_with("Script"); }
    // ItemHotkey::OnMapHotkey after the window gave `accel` for `type`: the
    // question to ask first, if any (the copy is filled before it).
    std::optional<HotkeyConflict> mapConflict(int row, const std::string &accel, int type, const HotkeyMap &live);
    // ...and the binding set after the answer (ignored without a question).
    void map(int row, const std::string &accel, int type, HotkeyAnswer answer, const HotkeyMap &live);
    // OnResetHotkey / OnDeleteHotkey.
    void reset(int row, const HotkeyMap &live);
    void remove(int row, const HotkeyMap &live);
    // Ctrl+Z / Ctrl+Y in the list.
    void undo();
    void redo();
    // SetOptions: with the list modified and the copy filled, the new live
    // bindings (the copy) after SaveAll; nothing otherwise.
    std::optional<HotkeyMap> commit();

private:
    // Rows are shared by the history's snapshots, as legacy's ItemRow
    // pointers are; CopyRow makes a new one.
    using RowPtr = std::shared_ptr<Row>;
    void ensureCopy(const HotkeyMap &live);
    void append(std::string text, std::string name, std::string accel, HotkeyId key);
    // CopyRow: row `y` becomes a new row (pushBack: a new row at the end).
    Row &copyRow(int y, bool pushBack = false);
    void pushHistory();
    void rebuildFiltered();
    int findShown(int row) const; // FindId
    void moveInHistory(int to);
    std::vector<HotkeyId> conflicts(int row, const std::string &accel) const;

    HotkeyMap &m_copy;
    std::vector<RowPtr> m_rows;               // itemList
    std::vector<RowPtr> m_filtered;           // filteredList
    std::vector<std::vector<RowPtr>> m_history; // historyList
    int m_iter = 0;
    bool m_modified = false;
    int m_sel = 0; // a position in m_filtered; -1 for none
    int m_filterMode = 0;
};

// Hotkeys::OnMapHkey (the Shift+click gesture): `id` gets `accel` for `type`
// in the live bindings. The question to ask first, if any:
std::optional<HotkeyConflict> mapHotkeyNowConflict(const HotkeyMap &live, int id, const std::string &accel, int type);
// ...and the change after the answer (Cancel changes nothing). Returns
// whether the audio file is saved too (a binding for the Audio window, or an
// Audio binding the question named).
struct HotkeyNowResult {
    bool changed = false;
    bool saveAudio = false;
};
HotkeyNowResult mapHotkeyNow(HotkeyMap &live, int id, const std::string &name, const std::string &accel, int type,
                             HotkeyAnswer answer);

} // namespace hikari::application
