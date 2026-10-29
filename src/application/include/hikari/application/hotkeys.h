#pragma once

// A binding is keyed by (id, window), the window it was mapped for, which may
// differ from the id's own; its keys are accelerator text ("Ctrl-Shift-O").

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

enum HotkeyWindow : int { GlobalHotkey = 0, GridHotkey, EditorHotkey, VideoHotkey, AudioHotkey };
inline constexpr int kHotkeyWindows = 5;

struct HotkeyId {
    int id = 0;
    int type = GlobalHotkey;
    friend bool operator==(const HotkeyId &, const HotkeyId &) = default;
    friend bool operator<(const HotkeyId &a, const HotkeyId &b)
    {
        return a.type != b.type ? a.type < b.type : a.id < b.id;
    }
};

struct Hotkey {
    std::string name;
    std::string accel;
    friend bool operator==(const Hotkey &, const Hotkey &) = default;
};
using HotkeyMap = std::map<HotkeyId, Hotkey>;

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
inline constexpr int kLegacyHotkeyActions = 243;
inline constexpr int kFirstScriptHotkey = 30100;

struct HotkeyAction {
    std::string_view symbol;
    int id;
};
// HikariSub legacy's kLegacyHotkeyActions symbols first, then HikariSub's own
// commands on ids HikariSub legacy left unused.
std::span<const HotkeyAction> hotkeyActions();
// 0 for an unknown symbol.
int hotkeyIdOf(std::string_view symbol);
// "" for an id without a symbol.
std::string_view hotkeySymbol(int id);
// The English names the Options list shows, by id.
const std::map<int, std::string> &hotkeyNames();
// "" when the id has no name (scripts, GLOBAL_QUIT...).
std::string hotkeyName(int id);
// A retired action keeps its symbol, so stored lines naming it are
// recognised, but has no name and its bindings are dropped as lists are read.
bool isRetiredHotkey(int id);
// Removes the retired actions' bindings from `map` and returns them, for the
// notice the reader gives.
std::vector<std::pair<HotkeyId, Hotkey>> dropRetiredHotkeys(HotkeyMap &map);
// The window an id's range belongs to.
int hotkeyType(int id);
// The window names the Options list prefixes ("Global", "Subtitles",
// "Editor", "Video", "Audio").
std::string_view hotkeyWindowName(int type);

// Assigns the defaults of the main (or audio) file over `map`.
void loadDefaultHotkeys(HotkeyMap &map, bool audio);
// Both defaults and nothing else.
HotkeyMap defaultHotkeys();
// The default accelerator of exactly (id, window), or "".
std::string defaultHotkey(const HotkeyId &key);

// The lines for the main (or audio) file, without its "[version]"
// header: AUDIO_HOTKEY bindings go to the audio file, the others to the main
// one; empty bindings, ids under 100 and script ids are not written (the
// script bindings are the automation hotkeys').
std::vector<std::string> hotkeyLines(const HotkeyMap &map, bool audio);
// "<SYMBOL or id> <G|S|E|V|A>=<accel>": an unknown symbol becomes id 0, an
// unknown window letter window -1, and an empty binding is skipped. A numeric
// label is read with hotkeyLabelNumber, which never fails. "Script ..." lines
// are skipped. The version header and record count are the importer's.
void readHotkeyLines(HotkeyMap &map, const std::vector<std::string> &lines);
void readHotkeyLines(HotkeyMap &map, const std::vector<std::string> &lines, bool windows);
// Windows' _wtoi clamps past the int range to INT_MAX / INT_MIN; Linux's
// atoi truncates a 64-bit long to int.
int hotkeyLabelNumber(std::string_view label, bool windows);

// The modifiers found anywhere ("Alt-", "Ctrl-", "Shift-") and the key after
// the last '-' (or '-' itself when it ends with one), a key name or one
// character. Returns a Qt portable sequence ("Ctrl+Shift+O", "Num+0",
// "Return"), or "" when the key is invalid.
std::string qtKeysOfAccel(std::string_view accel);
// The key text when it is neither a key name nor one UTF-16 unit; "" when
// valid. A key of one non-ASCII character is valid but has no Qt sequence.
std::string invalidHotkeyKey(std::string_view accel);
// The other way, for bindings captured as Qt portable sequences (the
// automation hotkeys'): accelerator names and modifier order ("Alt-Ctrl-Shift-").
std::string accelOfQtKeys(std::string_view keys);

// wxWidgets key codes.
namespace wxk {
inline constexpr int Back = 8, Tab = 9, Return = 13, Escape = 27, Space = 32, Delete = 127;
inline constexpr int Shift = 306, Alt = 307, Control = 308, Pause = 310, End = 312, Home = 313, Left = 314, Up = 315,
                     Right = 316, Down = 317, Insert = 322, Numpad0 = 324, F1 = 340, F4 = 343, F24 = 363,
                     PageUp = 366, PageDown = 367, NumpadEnter = 370, NumpadHome = 375, NumpadDelete = 385,
                     NumpadMultiply = 387, NumpadAdd = 388, NumpadSeparator = 389, NumpadSubtract = 390,
                     NumpadDecimal = 391, NumpadDivide = 392, WindowsLeft = 393;
}

// Key names by wx key code.
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
HotkeyCapture captureHotkey(const HotkeyPress &press, int type);

// The accelerators the Line editor's text fields keep whatever a binding says
// (numpad digits only when TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS is off).
bool textFieldOwnsKey(std::string_view accel, bool allowNumpadHotkeys);

// A "This hotkey already exists" / "This shortcut already exists in ..."
// question before a binding is set.
struct HotkeyConflict {
    bool doubled = false;     // the same window, or one treated as the same
    std::size_t count = 0;    // bindings holding the keys
    std::string message;
    bool canSwitch = false;   // "Switch hotkeys"
    bool canSetAnyway = false; // "Set anyway"
    std::vector<std::string> commands; // the names of the commands holding the keys
};
enum class HotkeyAnswer { Switch, Delete, SetAnyway, Cancel };

// `copy` is filled from the live bindings at the first edit while it is
// empty and is never cleared, so it outlives the page.
class HotkeyList {
public:
    struct Row {
        std::string text;  // the Function column: "<window> <name>"
        bool textModified = false;
        std::string name;
        std::string accel; // the Hotkey column
        bool keyModified = false;
        HotkeyId key;
        // MUST KILL: raw visibility, 0 hidden, 1 shown, 2 shown again from the gutter
        int visible = 1;
    };

    explicit HotkeyList(HotkeyMap &copy) : m_copy(copy) {}
    HotkeyList(const HotkeyList &) = delete;
    HotkeyList &operator=(const HotkeyList &) = delete;

    // Selects the first row. clear() keeps the history and the modified flag.
    void build(const HotkeyMap &live);
    void clear();
    // Rows added while the page is open (a macro registered meanwhile),
    // at the end and in every step of the history, shown as the filter shows
    // them; the edits, the history and the selection stay.
    void addRows(const HotkeyMap &more);
    // ...and the rows `gone` names that no step of the history changed (a
    // macro unloaded meanwhile), from the list and every step; an edited row
    // stays, as do the selection and the history.
    void removeUnchangedRows(const std::function<bool(const Row &)> &gone);

    std::size_t rowCount() const { return m_rows.size(); }
    const Row &row(int index) const { return *m_rows[std::size_t(index)]; }
    // The rows shown, in order (indexes into rows()).
    std::vector<int> shown() const;
    bool modified() const { return m_modified; }
    int filterMode() const { return m_filterMode; }
    // 0 All, 1 Set shortcuts, 2 Global, 3 Subtitles, 4 Editor, 5 Video,
    // 6 Audio, 7 Automation (the macros' rows). The selection goes to the
    // first shown row.
    void filter(int mode);
    // Within the filter, only rows whose Function or Hotkey column holds `text`
    // (ASCII case-insensitive; macros' rows also by `also`) show. Empty shows
    // every row the filter shows. The selection goes to the first shown row.
    // Edits, Undo and Redo search again.
    void search(std::string text, const std::function<std::string(const Row &)> &also = {});
    const std::string &searchText() const { return m_search; }
    // The selected row, or -1.
    int selection() const;
    void selectShown(int position);
    // The row whose Function column reads `text`, or -1.
    int find(const std::string &text) const;
    // The gutter while some row is hidden. hiddenBlock, for the gap after a
    // shown position (-1: above the first): 0 none, 1 hidden rows ("+"), 2 rows
    // shown from a block ("-"). toggleBlock switches the rows after that
    // position between hidden and shown from a block, up to the next shown one.
    bool filtered() const { return m_isFiltered; }
    int hiddenBlock(int position) const;
    void toggleBlock(int position);

    // The "Hotkey mapping" window's title name and whether it offers the
    // window choice (not for scripts).
    std::string mapName(int index) const { return row(index).name; }
    bool mapOffersWindows(int index) const { return !row(index).name.starts_with("Script"); }
    // After the mapping window gave `accel` for `type`: the question to ask
    // first, if any (the copy is filled before it).
    std::optional<HotkeyConflict> mapConflict(int row, const std::string &accel, int type, const HotkeyMap &live);
    // ...and the binding set after the answer (ignored without a question).
    void map(int row, const std::string &accel, int type, HotkeyAnswer answer, const HotkeyMap &live);
    void reset(int row, const HotkeyMap &live);
    // Every row back to its default binding as one history step; nothing when
    // every row has it already.
    void resetAll(const HotkeyMap &live);
    void remove(int row, const HotkeyMap &live);
    void undo();
    void redo();
    // With the list modified and the copy filled, the new live bindings (the
    // copy); nothing otherwise.
    std::optional<HotkeyMap> commit();

private:
    // Rows are shared by the history's snapshots; copyRow makes a new one.
    using RowPtr = std::shared_ptr<Row>;
    void ensureCopy(const HotkeyMap &live);
    void append(std::string text, std::string name, std::string accel, HotkeyId key);
    // Row `y` becomes a new row (pushBack: a new row at the end).
    Row &copyRow(int y, bool pushBack = false);
    void pushHistory();
    void rebuildFiltered();
    int findShown(int row) const;
    void moveInHistory(int to);
    // After an edit, while searching: the rows matching their bindings now,
    // the selected row kept if it still matches.
    void rerunSearch();
    std::vector<HotkeyId> conflicts(int row, const std::string &accel) const;

    HotkeyMap &m_copy;
    std::vector<RowPtr> m_rows;
    std::vector<RowPtr> m_filtered;
    std::vector<std::vector<RowPtr>> m_history;
    int m_iter = 0;
    bool m_modified = false;
    int m_sel = 0; // a position in m_filtered; -1 for none
    int m_filterMode = 0;
    std::string m_search; // lower case
    std::function<std::string(const Row &)> m_searchAlso;
    bool m_isFiltered = false;
    bool shownNow(const Row &r) const;
    int findKey(int position) const; // the row at a shown position, or -1
};

// Shift+click on a menu item or mapped button: `id` gets `accel` for `type`
// in the live bindings at once. The question to ask first, if any:
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

}
