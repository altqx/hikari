#pragma once

// Automation hotkeys (S2; legacy AutomationHotkeysDialog at 20d647c4). AM1:
// the "List of automation shortcuts" window is gone; each macro's shortcut
// is shown and recorded in the Automation panel and in Settings → Hotkeys,
// both editing legacy's one hotkey map (the script lines, by legacy name,
// with the macro identity each binding was made for). A binding that no
// longer resolves stays visible as missing (S44-macro-alias). Committed
// bindings that resolve become application-wide shortcuts running their
// macros. Bindings are kept in the settings registry
// (shortcuts.automationMacros).

#include "hikari/application/automation_hotkeys.h"
#include "hikari/application/automation_registry.h"

#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app {

class AutomationShell;

class AutomationHotkeysController : public QObject {
    Q_OBJECT
    // The committed bindings that resolve to a loaded macro: legacy name -> keys.
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY bindingsChanged)
    // The committed bindings that do not: [{legacyName, macro, keys, problem}].
    Q_PROPERTY(QVariantList missing READ missing NOTIFY bindingsChanged)
    // Installed bindings that resolve: {legacyName, keys}.
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
public:
    AutomationHotkeysController(AutomationShell &automation, ui::SettingsStore &settings, QObject *parent = nullptr);

    QVariantMap keys() const;
    QVariantList missing() const;
    QVariantList shortcuts() const;

    // A binding removed at once: saved and installed (the panel's Clear and
    // Remove, as the Shift+click gesture changes the map at once).
    Q_INVOKABLE void clearKeys(const QString &legacyName);
    // Runs the macro a committed shortcut names; false when it does not resolve.
    Q_INVOKABLE bool run(const QString &legacyName);
    // Adds the script hotkeys of a legacy Hotkeys.txt that have no binding
    // yet (current bindings are kept), saved and installed; returns how many
    // were added.
    Q_INVOKABLE int importLegacy(const QString &path);
    // The portable sequence for a key press ("" for a lone modifier).
    Q_INVOKABLE QString keysOf(int key, int modifiers) const;
    // "Script <file name>-<ordinal>" for a registered macro.
    Q_INVOKABLE QString legacyNameFor(const QString &scriptPath, int ordinal) const;

    // O2: the script bindings are the script lines of legacy's one hotkey map,
    // which the shortcut editor also edits. The committed keys by legacy name;
    // replacing them changes neither the stored nor the installed bindings
    // until save() and install() (legacy SaveHkeys, SetAccels).
    std::map<std::string, std::string> committedKeys() const;
    // Keys that change are a binding made now: it records the macro loaded
    // now, as recording the shortcut in the panel does.
    void replaceCommitted(const std::map<std::string, std::string> &keys);
    // A shortcut recorded again with the keys it has: the binding is made for
    // the macro loaded now (one missing since its script changed resolves
    // again), saved and installed. False when nothing changed.
    bool renew(const QString &legacyName);
    void save() const;
    void install();

    // AM1, Settings → Hotkeys: the legacy names of the registered macros
    // that have no binding (listed so they can be mapped there too), and a
    // binding's macro as shown: {script, macro, problem} ("problem" empty
    // while it resolves).
    std::vector<std::string> unboundMacros() const;
    QVariantMap describe(const std::string &legacyName) const;

signals:
    void bindingsChanged();
    void shortcutsChanged();
    // A change made here, saved: legacy then saves and installs the whole map.
    void committed();

private:
    void load();
    application::AliasResolution resolution(const application::MacroBinding &binding) const;
    std::optional<std::pair<std::string, int>> resolve(const application::MacroBinding &binding) const;
    void recordIdentity(application::MacroBinding &binding) const;
    QString scriptLabel(const std::string &path) const;
    void commitChange();

    AutomationShell &m_automation;
    ui::SettingsStore &m_settings;
    std::map<std::string, application::MacroBinding> m_committed;
    std::map<std::string, application::MacroBinding> m_installed; // the shortcuts in effect
};

} // namespace hikari::app
