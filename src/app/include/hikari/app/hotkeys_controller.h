#pragma once

#include "hikari/application/hotkeys.h"

#include "settings_store.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace hikari::app {

class AutomationHotkeysController;

class HotkeysController : public QObject {
    Q_OBJECT
    // {global|grid|editor|video|audio: {symbol: Qt sequence}} of the installed bindings.
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY installedChanged)
    // [{symbol, keys}] per installed Global binding with a Qt sequence; first in map order wins.
    // An audio _ALT id is listed as its main id, only while the main id has a Global binding.
    Q_PROPERTY(QVariantList globalShortcuts READ globalShortcuts NOTIFY installedChanged)
    // globalShortcuts' keys, as portable text.
    Q_PROPERTY(QStringList globalSequences READ globalSequences NOTIFY installedChanged)
    // globalSequences and the Video window's Play / Pause keys.
    Q_PROPERTY(QStringList macroTakenSequences READ macroTakenSequences NOTIFY installedChanged)
    // {row, text, context, name, accel, textModified, keyModified, block, inBlock}, and
    // {script, macro, note} on a macro's row.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    // A position among the shown rows, -1 when none.
    Q_PROPERTY(int selected READ selected NOTIFY rowsChanged)
    Q_PROPERTY(int filterMode READ filterMode NOTIFY rowsChanged)
    Q_PROPERTY(bool optionsModified READ optionsModified NOTIFY rowsChanged)
    // topBlock and a row's "block": 0 none, 1 hidden rows ("+"), 2 rows shown from a block ("-").
    Q_PROPERTY(bool listFiltered READ listFiltered NOTIFY rowsChanged)
    Q_PROPERTY(int topBlock READ topBlock NOTIFY rowsChanged)
    // Why the selected row cannot be given keys, "" when it can.
    Q_PROPERTY(QString mapProblem READ mapProblem NOTIFY rowsChanged)
public:
    HotkeysController(AutomationHotkeysController &scripts, ui::SettingsStore &settings,
                      std::function<void(const QString &, bool warning)> log = {}, QObject *parent = nullptr);
    ~HotkeysController() override;

    QVariantMap keys() const;
    QVariantList globalShortcuts() const;
    QStringList globalSequences() const;
    QStringList macroTakenSequences() const;
    QVariantList rows() const;
    bool listFiltered() const;
    int topBlock() const;
    int selected() const;
    int filterMode() const;

    // The bindings in memory, scripts included.
    application::HotkeyMap bindings() const;
    const application::HotkeyMap &installed() const { return m_installed; }
    // The installed accelerator text ("Ctrl-S") of symbol in window, "" when none.
    Q_INVOKABLE QString accelOf(const QString &symbol, int window) const;

    // The symbol a key press runs in window, "" when none; an _ALT audio id answers as its main id.
    // textField false: the key was pressed in the Line editor outside its text fields.
    Q_INVOKABLE QString actionFor(int window, int key, int modifiers, bool textField = true) const;
    // True (ignore it) when the same action came within interval ms of the last one not ignored.
    Q_INVOKABLE bool repeatedKey(const QString &action, int interval = 100);
    // Tests: the millisecond clock repeatedKey reads.
    void setKeyClock(std::function<qint64()> clock) { m_keyClock = std::move(clock); }
    // Emits ran and returns handled; while rehearsing (tests) callers report and do not run.
    Q_INVOKABLE bool report(const QString &symbol, bool handled)
    {
        emit ran(symbol, handled);
        return handled;
    }
    Q_INVOKABLE bool rehearsing() const { return m_rehearsing; }
    void setRehearsing(bool on) { m_rehearsing = on; }

    Q_INVOKABLE void beginOptions();
    Q_INVOKABLE void filter(int mode);
    // Also matches a macro's row by its script and macro names.
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void select(int position);
    // The gutter box after a shown position; -1 is the one above the first.
    Q_INVOKABLE void toggleBlock(int position);
    // {name, windows, type} for the selected row, {} without a selection.
    Q_INVOKABLE QVariantMap mapTarget() const;
    QString mapProblem() const;
    // {} when nothing conflicts; optionsMap's answer: "switch", "delete", "anyway" or "cancel".
    Q_INVOKABLE QVariantMap optionsConflict(const QString &accel, int type);
    Q_INVOKABLE void optionsMap(const QString &accel, int type, const QString &answer);
    Q_INVOKABLE void optionsReset();
    // Every row back to its default, staged like the other edits.
    Q_INVOKABLE void optionsResetAll();
    Q_INVOKABLE void optionsDelete();
    Q_INVOKABLE void optionsUndo();
    Q_INVOKABLE void optionsRedo();
    Q_INVOKABLE void commitOptions();
    // AutomationHotkeysController::importLegacy's result; {read: false} while the page holds edits.
    Q_INVOKABLE QVariantMap importLegacyScripts(const QString &path);
    bool optionsModified() const;
    // In memory only: nothing is saved or installed.
    void resetDefaults();

    // {result: "ignored" | "refused" | "accepted", accel, keys (portable text), message}.
    Q_INVOKABLE QVariantMap capture(int key, int modifiers, int window) const;

    // The last input was a button or menu click with Shift alone (exact) or with Shift held.
    Q_INVOKABLE bool shiftClicked(bool exact = true) const;
    Q_INVOKABLE bool ctrlClicked() const { return m_lastClickCtrlHeld; }
    // {id, name}; second names a two-hotkey button's second hotkey, the main id - 10.
    Q_INVOKABLE QVariantMap gestureTarget(const QString &symbolOrScript, bool second = false) const;
    Q_INVOKABLE QVariantMap gestureConflict(int id, const QString &accel, int type) const;
    Q_INVOKABLE void gestureMap(int id, const QString &name, const QString &accel, int type, const QString &answer);

    // The actions other than id bound to accel in any window, scripts included, as shown.
    Q_INVOKABLE QStringList holdersOf(const QString &accel, int id) const;
    // The command whose installed Global binding takes keys (portable text), "" when none.
    Q_INVOKABLE QString globalHolder(const QString &keys) const;

    // From a hotkey, "ignore filtering" is the opposite of the stored option.
    Q_INVOKABLE bool ignoreFilteringFromOption() const;

signals:
    void installedChanged();
    void rowsChanged();
    void ran(const QString &symbol, bool handled);

private:
    void load();
    void saveMain();
    void saveAudio();
    void setAccels();
    void logInvalid() const;
    void split(const application::HotkeyMap &combined);
    application::HotkeyMap listed() const;
    void relistMacros();
    bool eventFilter(QObject *watched, QEvent *event) override;

    AutomationHotkeysController &m_scripts;
    ui::SettingsStore &m_settings;
    application::HotkeyMap m_live;
    application::HotkeyMap m_installed;
    application::HotkeyMap m_copy;
    std::unique_ptr<application::HotkeyList> m_list;
    int m_installedTagButtons = 0;
    bool m_lastClickShift = false;
    bool m_lastClickShiftHeld = false;
    bool m_lastClickCtrlHeld = false;
    std::function<void(const QString &, bool warning)> m_log;
    bool m_rehearsing = false;
    QStringList m_retiredNotices;
    std::function<qint64()> m_keyClock;
    qint64 m_lastCheckedTime = 0;
    QString m_lastCheckedId;
};

}
