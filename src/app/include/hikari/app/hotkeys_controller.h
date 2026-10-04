#pragma once

// O2: the shortcut editor (legacy Hotkeys, HkeysDialog and the Options
// dialog's Hotkeys page at 20d647c4) over application::hotkeys.
// - Bindings in memory are legacy's Hkeys map: the static actions here, the
//   script ones (ids from 30100) in the automation hotkeys (S2), which keep
//   their macro identity. They are stored in the settings registry when
//   legacy saves its files (shortcuts.hotkeys: Hotkeys.txt's lines without
//   the scripts, shortcuts.audioHotkeys: AudioHotkeys.txt's) and installed
//   (the shortcuts in effect) when legacy calls SetAccels.
// - The Options page edits a copy of the bindings of its own (legacy's
//   static hotkeysCopy outlived the dialog: O2-stale-copy); OK/Apply make
//   it the bindings, save and install them.
// - "Set default" (ResetDefault) replaces the bindings in memory only.
// - The Shift+click gesture on a menu item or a mapped button (OnMapHkey)
//   changes, installs and saves the bindings at once.

#include "hikari/application/hotkeys.h"

#include "settings_store.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace hikari::app {

class AutomationHotkeysController;

class HotkeysController : public QObject {
    Q_OBJECT
    // The installed bindings that have a Qt sequence, by window and symbol:
    // {global: {GLOBAL_SAVE_SUBS: "Ctrl+S", ...}, grid: ..., editor: ...,
    // video: ..., audio: ...}.
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY installedChanged)
    // The Global window's accelerator table (HikariSubFrame::SetAccels,
    // HikariSubFrame.cpp:1754-1800): [{symbol, keys}] for every installed
    // Global binding with a Qt sequence, the first in map order for keys
    // held twice (the table's first entry wins). An action id under 5000
    // bound for the Global window is listed too (OnUseWindowHotkey runs it
    // in its own window); an audio _ALT id as its main id (GetHKey's id + 10),
    // only while that main id has a Global binding (the frame binds only
    // ids it installs). The scripts' bindings are S2's.
    Q_PROPERTY(QVariantList globalShortcuts READ globalShortcuts NOTIFY installedChanged)
    // Their keys (portable text), for other shortcuts to give way to them.
    Q_PROPERTY(QStringList globalSequences READ globalSequences NOTIFY installedChanged)
    // The Options page: the shown rows {row, text, accel, modified}, the
    // selected shown position and the filter mode.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int selected READ selected NOTIFY rowsChanged)
    Q_PROPERTY(int filterMode READ filterMode NOTIFY rowsChanged)
    // HikariListCtrl's gutter: the list is filtered (isFiltered), and the
    // block above the first shown row (CheckIfHasHiddenBlock(-1)); each row
    // carries its own ("block": 0, 1 "+", 2 "-"; "inBlock": shown from one).
    Q_PROPERTY(bool listFiltered READ listFiltered NOTIFY rowsChanged)
    Q_PROPERTY(int topBlock READ topBlock NOTIFY rowsChanged)
public:
    // `log`: HikariLog (the log window), for GetHKey's 'Shortcut "%s" is invalid'.
    HotkeysController(AutomationHotkeysController &scripts, ui::SettingsStore &settings,
                      std::function<void(const QString &)> log = {}, QObject *parent = nullptr);
    ~HotkeysController() override;

    QVariantMap keys() const;
    QVariantList globalShortcuts() const;
    QStringList globalSequences() const;
    QVariantList rows() const;
    bool listFiltered() const;
    int topBlock() const;
    int selected() const;
    int filterMode() const;

    // The bindings in memory, scripts included (legacy Hkeys.GetHotkeysMap()).
    application::HotkeyMap bindings() const;
    const application::HotkeyMap &installed() const { return m_installed; }
    // The installed binding of a symbol in `window`, as legacy text ("" when none).
    Q_INVOKABLE QString accelOf(const QString &symbol, int window) const;

    // The action a key press runs in `window`, as legacy's accelerator
    // tables route it ("" when none): Editor (EditBox: the Editor bindings
    // and Video actions bound for it; not the keys the text field keeps),
    // Subtitles (the Grid: its own and the Editor and Video actions bound for
    // it, Video's play and seek bindings, and the Audio bindings but
    // AUDIO_COMMIT to AUDIO_NEXT), Video (VideoBox: its own, and Editor and
    // Grid actions bound for it), Audio (AudioBox: the Audio bindings). An
    // _ALT audio id answers as its main id (legacy GetHKey's id + 10).
    // `textField` false: a key in the Editor window outside its text fields
    // (EditBox's table without DialogueTextEditor's own keys).
    Q_INVOKABLE QString actionFor(int window, int key, int modifiers, bool textField = true) const;
    // Config::CheckLastKeyEvent (config.cpp:1033-1045), shared by the frame's
    // OnMenuSelected1, SubsGrid, AudioBox (100 ms) and VideoBox (50 ms)
    // accelerators: true (ignore it) when the same action came less than
    // `interval` ms after the last one that was not ignored.
    Q_INVOKABLE bool repeatedKey(const QString &action, int interval = 100);
    // Tests: the millisecond clock CheckLastKeyEvent reads (timeGetTime).
    void setKeyClock(std::function<qint64()> clock) { m_keyClock = std::move(clock); }

    // The Options page (legacy OptionsDialog's constructor, AddHotkeysOnList).
    Q_INVOKABLE void beginOptions();
    Q_INVOKABLE void filter(int mode);
    Q_INVOKABLE void select(int position);
    // A click on the gutter's box after a shown position (-1: above the first).
    Q_INVOKABLE void toggleBlock(int position);
    // The selected row's "Hotkey mapping" window: {name, windows, type}, or
    // {} without a selection.
    Q_INVOKABLE QVariantMap mapTarget() const;
    // ItemHotkey::OnMapHotkey: the question first ({} when none: map with
    // any answer), then the answer ("switch", "delete", "anyway", "cancel").
    Q_INVOKABLE QVariantMap optionsConflict(const QString &accel, int type);
    Q_INVOKABLE void optionsMap(const QString &accel, int type, const QString &answer);
    Q_INVOKABLE void optionsReset();
    Q_INVOKABLE void optionsDelete();
    Q_INVOKABLE void optionsUndo();
    Q_INVOKABLE void optionsRedo();
    // OK/Apply (SetOptions).
    Q_INVOKABLE void commitOptions();
    // "Set default" (ResetDefault): Hkeys.ResetDefaults() and the list again.
    void resetDefaults();

    // HkeysDialog::OnKeyPress for a Qt key press in `window`:
    // {result: "ignored" | "refused" | "accepted", accel, message}.
    Q_INVOKABLE QVariantMap capture(int key, int modifiers, int window) const;

    // Hotkeys::OnMapHkey. The last input was a click on a button or menu
    // item with Shift alone (`exact`: a menu's wxMOD_SHIFT) or with Shift
    // held (a mapped button's ShiftDown()): it maps its hotkey instead of acting.
    Q_INVOKABLE bool shiftClicked(bool exact = true) const;
    // MappedButton with twoHotkeys: a click with Ctrl held maps too
    // (MappedButton.cpp:437-445).
    Q_INVOKABLE bool ctrlClicked() const { return m_lastClickCtrlHeld; }
    // {id, name}: the action's id and the name the mapping window shows (the
    // names table's; for a script, its legacy name).
    Q_INVOKABLE QVariantMap gestureTarget(const QString &symbolOrScript) const;
    Q_INVOKABLE QVariantMap gestureConflict(int id, const QString &accel, int type) const;
    Q_INVOKABLE void gestureMap(int id, const QString &name, const QString &accel, int type, const QString &answer);

    // GRID_FILTER_IGNORE_IN_ACTIONS through a hotkey (SubsGrid::OnAccelerator):
    // "ignore filtering" becomes the opposite of the stored option.
    Q_INVOKABLE bool ignoreFilteringFromOption() const;

signals:
    void installedChanged();
    void rowsChanged();

private:
    void load();
    // Legacy SaveHkeys(false) (the scripts are its lines too) and SaveHkeys(true).
    void saveMain();
    void saveAudio();
    void setAccels();
    void logInvalid() const;
    void split(const application::HotkeyMap &combined);
    bool eventFilter(QObject *watched, QEvent *event) override;

    AutomationHotkeysController &m_scripts;
    ui::SettingsStore &m_settings;
    application::HotkeyMap m_live;      // the static bindings in memory
    application::HotkeyMap m_installed; // the bindings in effect, scripts excluded
    application::HotkeyMap m_copy;      // OptionsDialog::hotkeysCopy, one per dialog (O2-stale-copy)
    std::unique_ptr<application::HotkeyList> m_list;
    int m_installedTagButtons = 0; // EDITBOX_TAG_BUTTONS when installed
    bool m_lastClickShift = false;     // Shift alone
    bool m_lastClickShiftHeld = false; // Shift with any other modifiers
    bool m_lastClickCtrlHeld = false;  // Ctrl with any other modifiers
    std::function<void(const QString &)> m_log;
    std::function<qint64()> m_keyClock;
    qint64 m_lastCheckedTime = 0; // Config::lastCheckedTime
    QString m_lastCheckedId;      // Config::lastCheckedId
};

} // namespace hikari::app
