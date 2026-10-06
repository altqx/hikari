#include "hikari/app/automation_hotkeys_controller.h"

#include "hikari/app/automation_shell.h"

#include <QFile>
#include <QKeySequence>
#include <QFileInfo>
#include <QVariantMap>

#include <set>

namespace hikari::app {

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString fileNameOf(const std::string &path)
{
    return QFileInfo(QString::fromStdString(path)).fileName();
}

} // namespace

AutomationHotkeysController::AutomationHotkeysController(AutomationShell &automation, ui::SettingsStore &settings,
                                                         QObject *parent)
    : QObject(parent), m_automation(automation), m_settings(settings)
{
    load();
    m_staged = m_committed;
    m_installed = m_committed;
    // Scripts loading or reloading change what resolves (the manager's single
    // observer belongs to the manager tool, which announces each change).
    connect(m_automation.managerController(), &ui::AutomationManagerController::changed, this, [this] {
        emit rowsChanged();
        emit shortcutsChanged();
    });
}

// One "legacy name\tkeys\tmacro\tsha256" row per binding.
void AutomationHotkeysController::load()
{
    for (const QString &row : m_settings.list(application::kAutomationHotkeysSetting.data())) {
        const QStringList v = row.split(QLatin1Char('\t'));
        application::MacroBinding b;
        b.legacyName = v.value(0).toStdString();
        b.keys = v.value(1).toStdString();
        b.macroName = v.value(2).toStdString();
        b.scriptSha256 = v.value(3).toStdString();
        if (!b.keys.empty())
            m_committed[b.legacyName] = b;
    }
}

void AutomationHotkeysController::save() const
{
    QStringList rows;
    for (const auto &[name, b] : m_committed)
        rows << QStringList{qs(name), qs(b.keys), qs(b.macroName), qs(b.scriptSha256)}.join(QLatin1Char('\t'));
    m_settings.set(application::kAutomationHotkeysSetting.data(), rows);
}

std::optional<std::pair<std::string, int>>
AutomationHotkeysController::resolve(const application::MacroBinding &binding) const
{
    const auto alias = application::aliasOfLegacyName(binding.legacyName);
    if (!alias)
        return std::nullopt;
    std::optional<application::MacroIdentity> recorded;
    if (!binding.macroName.empty()) {
        application::MacroIdentity r;
        r.name = binding.macroName;
        r.scriptSha256 = binding.scriptSha256;
        recorded = r;
    }
    const auto resolution = m_automation.manager().registry().resolve(*alias, recorded);
    if (!resolution.identity)
        return std::nullopt;
    return std::pair(resolution.identity->scriptPath, resolution.identity->ordinal);
}

QVariantList AutomationHotkeysController::rows() const
{
    QVariantList out;
    std::set<std::string> shown;
    for (const auto &m : m_automation.manager().registry().macros()) {
        const std::string name = application::legacyNameOf(fileNameOf(m.scriptPath).toStdString(), m.ordinal);
        const auto it = m_staged.find(name);
        QString problem;
        if (it != m_staged.end() && !resolve(it->second))
            problem = tr("Unresolved: the script or its macros changed");
        out << QVariantMap{{QStringLiteral("legacyName"), qs(name)},
                           {QStringLiteral("script"), qs(m.scriptPath)},
                           {QStringLiteral("macro"), qs(m.name)},
                           {QStringLiteral("keys"), it != m_staged.end() ? qs(it->second.keys) : QString()},
                           {QStringLiteral("problem"), problem}};
        shown.insert(name);
    }
    // Bindings whose macro is not loaded stay visible.
    for (const auto &[name, b] : m_staged)
        if (!shown.contains(name))
            out << QVariantMap{{QStringLiteral("legacyName"), qs(name)},
                               {QStringLiteral("script"), QString()},
                               {QStringLiteral("macro"), qs(b.macroName)},
                               {QStringLiteral("keys"), qs(b.keys)},
                               {QStringLiteral("problem"), tr("Unresolved: no loaded script has this macro")}};
    return out;
}

QVariantList AutomationHotkeysController::shortcuts() const
{
    QVariantList out;
    for (const auto &[name, b] : m_installed)
        if (resolve(b))
            out << QVariantMap{{QStringLiteral("legacyName"), qs(name)}, {QStringLiteral("keys"), qs(b.keys)}};
    return out;
}

void AutomationHotkeysController::begin()
{
    m_staged = m_committed;
    emit rowsChanged();
}

QString AutomationHotkeysController::conflict(const QString &legacyName, const QString &keys) const
{
    for (const auto &[name, b] : m_staged)
        if (qs(name) != legacyName && qs(b.keys) == keys)
            return qs(name);
    return {};
}

void AutomationHotkeysController::setKeys(const QString &legacyName, const QString &keys)
{
    if (keys.isEmpty())
        return clearKeys(legacyName);
    for (auto it = m_staged.begin(); it != m_staged.end();)
        it = qs(it->first) != legacyName && qs(it->second.keys) == keys ? m_staged.erase(it) : std::next(it);
    application::MacroBinding b;
    b.legacyName = legacyName.toStdString();
    b.keys = keys.toStdString();
    // Recorded with the registration it was made for.
    for (const auto &m : m_automation.manager().registry().macros())
        if (application::legacyNameOf(fileNameOf(m.scriptPath).toStdString(), m.ordinal) == b.legacyName) {
            b.macroName = m.name;
            b.scriptSha256 = m.scriptSha256;
        }
    m_staged[b.legacyName] = b;
    emit rowsChanged();
}

void AutomationHotkeysController::clearKeys(const QString &legacyName)
{
    m_staged.erase(legacyName.toStdString());
    emit rowsChanged();
}

void AutomationHotkeysController::commit()
{
    // Legacy's OK acts on the whole map only after a change in the window.
    std::map<std::string, std::string> staged;
    for (const auto &[name, b] : m_staged)
        staged[name] = b.keys;
    const bool changed = staged != committedKeys();
    m_committed = m_staged;
    save();
    m_installed = m_committed;
    emit rowsChanged();
    emit shortcutsChanged();
    if (changed)
        emit committed();
}

std::map<std::string, std::string> AutomationHotkeysController::committedKeys() const
{
    std::map<std::string, std::string> out;
    for (const auto &[name, b] : m_committed)
        out[name] = b.keys;
    return out;
}

void AutomationHotkeysController::replaceCommitted(const std::map<std::string, std::string> &keys)
{
    std::map<std::string, application::MacroBinding> next;
    for (const auto &[name, k] : keys) {
        if (k.empty())
            continue;
        const auto kept = m_committed.find(name);
        application::MacroBinding b;
        if (kept != m_committed.end()) {
            b = kept->second; // the registration it was made for
        } else {
            b.legacyName = name;
            for (const auto &m : m_automation.manager().registry().macros())
                if (application::legacyNameOf(fileNameOf(m.scriptPath).toStdString(), m.ordinal) == name) {
                    b.macroName = m.name;
                    b.scriptSha256 = m.scriptSha256;
                }
        }
        b.keys = k;
        next[name] = std::move(b);
    }
    m_committed = std::move(next);
    m_staged = m_committed;
    emit rowsChanged();
}

void AutomationHotkeysController::install()
{
    m_installed = m_committed;
    emit shortcutsChanged();
}

QString AutomationHotkeysController::legacyNameFor(const QString &scriptPath, int ordinal) const
{
    return qs(application::legacyNameOf(QFileInfo(scriptPath).fileName().toStdString(), ordinal));
}

void AutomationHotkeysController::cancel()
{
    m_staged = m_committed;
    emit rowsChanged();
}

bool AutomationHotkeysController::run(const QString &legacyName)
{
    const auto it = m_committed.find(legacyName.toStdString());
    if (it == m_committed.end())
        return false;
    const auto target = resolve(it->second);
    // HikariSubFrame::OnRunScript: validated first, failure said (S4).
    return target && m_automation.run(target->first, target->second, AutomationShell::RunOrigin::Hotkey);
}

int AutomationHotkeysController::importLegacy(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray bytes = f.readAll();
    int added = 0;
    for (auto &b : application::parseLegacyScriptHotkeys(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))) {
        if (m_staged.contains(b.legacyName))
            continue; // the current binding is kept
        bool taken = false;
        for (const auto &[name, existing] : m_staged)
            taken = taken || existing.keys == b.keys;
        if (taken)
            continue; // the keys already belong to another macro
        m_staged[b.legacyName] = std::move(b);
        ++added;
    }
    emit rowsChanged();
    return added;
}

QString AutomationHotkeysController::keysOf(int key, int modifiers) const
{
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == 0 ||
        key == Qt::Key_unknown)
        return {};
    const auto mods = Qt::KeyboardModifiers(modifiers) &
                      (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier | Qt::KeypadModifier);
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}

} // namespace hikari::app
