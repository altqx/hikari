#pragma once

// The "List of automation shortcuts" window (S2; legacy
// AutomationHotkeysDialog at 20d647c4): every registered macro with its
// shortcut, Map hotkey / Delete hotkey edited in the window and applied on
// OK, bindings that no longer resolve shown as unresolved (S44-macro-alias),
// and an import of a legacy Hotkeys.txt's script lines. Committed bindings
// that resolve become application-wide shortcuts running their macros.
// Bindings are kept in the settings registry (shortcuts.automationMacros).

#include "hikari/application/automation_hotkeys.h"

#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <map>

namespace hikari::app {

class AutomationShell;

class AutomationHotkeysController : public QObject {
    Q_OBJECT
    // Window rows: {legacyName, script, macro, keys, problem}.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    // Committed bindings that resolve: {legacyName, keys}.
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
public:
    AutomationHotkeysController(AutomationShell &automation, ui::SettingsStore &settings, QObject *parent = nullptr);

    QVariantList rows() const;
    QVariantList shortcuts() const;

    // Opening the window starts from the committed bindings.
    Q_INVOKABLE void begin();
    // Map hotkey: the macro that already has `keys`, or "" when free.
    Q_INVOKABLE QString conflict(const QString &legacyName, const QString &keys) const;
    // Sets the shortcut; another macro holding the same keys loses them (the
    // window asks first, as legacy does).
    Q_INVOKABLE void setKeys(const QString &legacyName, const QString &keys);
    Q_INVOKABLE void clearKeys(const QString &legacyName);
    Q_INVOKABLE void commit(); // OK
    Q_INVOKABLE void cancel();
    // Runs the macro a committed shortcut names; false when it does not resolve.
    Q_INVOKABLE bool run(const QString &legacyName);
    // Adds the script hotkeys of a legacy Hotkeys.txt that have no binding
    // yet (current bindings are kept); returns how many were added.
    Q_INVOKABLE int importLegacy(const QString &path);
    // The portable sequence for a key press in the "Hotkey mapping" window
    // ("" for a lone modifier).
    Q_INVOKABLE QString keysOf(int key, int modifiers) const;

    // O2: the script bindings are the script lines of legacy's one hotkey map,
    // which the shortcut editor also edits. The committed keys by legacy name;
    // replacing them changes neither the stored nor the installed bindings
    // until save() and install() (legacy SaveHkeys, SetAccels).
    std::map<std::string, std::string> committedKeys() const;
    void replaceCommitted(const std::map<std::string, std::string> &keys);
    void save() const;
    void install();
    // "Script <file name>-<ordinal>" for a registered macro.
    Q_INVOKABLE QString legacyNameFor(const QString &scriptPath, int ordinal) const;

signals:
    void rowsChanged();
    void shortcutsChanged();
    // OK in the window after a change (legacy then saves and installs the whole map).
    void committed();

private:
    void load();
    std::optional<std::pair<std::string, int>> resolve(const application::MacroBinding &binding) const;

    AutomationShell &m_automation;
    ui::SettingsStore &m_settings;
    std::map<std::string, application::MacroBinding> m_committed;
    std::map<std::string, application::MacroBinding> m_staged;
    std::map<std::string, application::MacroBinding> m_installed; // the shortcuts in effect
};

} // namespace hikari::app
