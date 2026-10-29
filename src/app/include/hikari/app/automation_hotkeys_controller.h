#pragma once

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
    // Committed bindings that resolve: legacy name -> keys.
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY bindingsChanged)
    // Committed bindings that do not resolve: [{legacyName, macro, keys, problem}].
    Q_PROPERTY(QVariantList missing READ missing NOTIFY bindingsChanged)
    // Installed bindings that resolve: {legacyName, keys}.
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
public:
    AutomationHotkeysController(AutomationShell &automation, ui::SettingsStore &settings, QObject *parent = nullptr);

    QVariantMap keys() const;
    QVariantList missing() const;
    QVariantList shortcuts() const;

    // Saves and installs at once.
    Q_INVOKABLE void clearKeys(const QString &legacyName);
    // False when the shortcut does not resolve.
    Q_INVOKABLE bool run(const QString &legacyName);
    // Adds Hotkeys.txt script bindings that have no binding yet, then saves and installs.
    // Returns {read (false: unreadable), added, kept (already bound), skipped (keys taken),
    // skippedNames ("Script <file>-<n> (<keys>)" each)}.
    Q_INVOKABLE QVariantMap importLegacy(const QString &path);
    // "" for a lone modifier.
    Q_INVOKABLE QString keysOf(int key, int modifiers) const;
    // "Script <file name>-<ordinal>" for a registered macro.
    Q_INVOKABLE QString legacyNameFor(const QString &scriptPath, int ordinal) const;
    // Empty when `legacyName` can be given a shortcut; a name several loaded scripts share cannot.
    Q_INVOKABLE QString recordingProblem(const QString &legacyName) const;

    // Legacy name -> keys. Replacing them takes effect only after save() and install().
    std::map<std::string, std::string> committedKeys() const;
    // A binding whose keys change is rebound to the macro loaded now.
    void replaceCommitted(const std::map<std::string, std::string> &keys);
    // Rebinds to the macro loaded now, then saves and installs. False when nothing changed.
    bool renew(const QString &legacyName);
    void save() const;
    void install();

    std::vector<std::string> unboundMacros() const;
    // {script, macro, problem}; problem is empty while the binding resolves.
    QVariantMap describe(const std::string &legacyName) const;

signals:
    void bindingsChanged();
    void shortcutsChanged();
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
    std::map<std::string, application::MacroBinding> m_installed;
};

}
