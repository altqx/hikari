#pragma once

// O2: the shortcut editor (legacy Hotkeys, HkeysDialog and the Options
// dialog's Hotkeys page at 20d647c4) over application::hotkeys.
// - Bindings in memory are legacy's Hkeys map: the static actions here, the
//   script ones (ids from 30100) in the automation hotkeys (S2), which keep
//   their macro identity. They are stored in the settings registry when
//   legacy saves its files (shortcuts.hotkeys: Hotkeys.txt's lines without
//   the scripts, shortcuts.audioHotkeys: AudioHotkeys.txt's) and installed
//   (the shortcuts in effect) when legacy calls SetAccels.
// - The Options page edits legacy's static hotkeysCopy, which outlives the
//   dialog; OK/Apply make it the bindings, save and install them.
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
    // The Options page: the shown rows {row, text, accel, modified}, the
    // selected shown position and the filter mode.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int selected READ selected NOTIFY rowsChanged)
    Q_PROPERTY(int filterMode READ filterMode NOTIFY rowsChanged)
public:
    HotkeysController(AutomationHotkeysController &scripts, ui::SettingsStore &settings, QObject *parent = nullptr);
    ~HotkeysController() override;

    QVariantMap keys() const;
    QVariantList rows() const;
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
    // it, and Video's play and seek bindings), Video (VideoBox: its own, and
    // Editor and Grid actions bound for it).
    Q_INVOKABLE QString actionFor(int window, int key, int modifiers) const;

    // The Options page (legacy OptionsDialog's constructor, AddHotkeysOnList).
    Q_INVOKABLE void beginOptions();
    Q_INVOKABLE void filter(int mode);
    Q_INVOKABLE void select(int position);
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
    void split(const application::HotkeyMap &combined);
    bool eventFilter(QObject *watched, QEvent *event) override;

    AutomationHotkeysController &m_scripts;
    ui::SettingsStore &m_settings;
    application::HotkeyMap m_live;      // the static bindings in memory
    application::HotkeyMap m_installed; // the bindings in effect, scripts excluded
    application::HotkeyMap m_copy;      // OptionsDialog::hotkeysCopy
    std::unique_ptr<application::HotkeyList> m_list;
    int m_installedTagButtons = 0; // EDITBOX_TAG_BUTTONS when installed
    bool m_lastClickShift = false;     // Shift alone
    bool m_lastClickShiftHeld = false; // Shift with any other modifiers
};

} // namespace hikari::app
