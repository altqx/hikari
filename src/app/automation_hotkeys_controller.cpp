#include "hikari/app/automation_hotkeys_controller.h"

#include "hikari/app/automation_shell.h"

#include <QCoreApplication>
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

// Legacy files write modifiers as "Alt-Ctrl-K"; keys Qt cannot read stay as given.
std::string canonicalKeys(const std::string &keys)
{
    const QKeySequence seq = QKeySequence::fromString(qs(keys), QKeySequence::PortableText);
    if (seq.count() != 1 || seq[0].key() == Qt::Key_unknown)
        return keys;
    return seq.toString(QKeySequence::PortableText).toStdString();
}

QString problemText(application::AliasProblem problem)
{
    using application::AliasProblem;
    switch (problem) {
    case AliasProblem::MissingScript:
        return QCoreApplication::translate("HikariSub.Automation", "No loaded script has this macro");
    case AliasProblem::BasenameCollision:
        return QCoreApplication::translate("HikariSub.Automation", "Several loaded scripts have this file name");
    case AliasProblem::OrdinalOutOfRange:
        return QCoreApplication::translate("HikariSub.Automation", "The script has no such macro");
    case AliasProblem::RegistrationChanged:
        return QCoreApplication::translate("HikariSub.Automation", "The script or its macro changed");
    case AliasProblem::MalformedAlias:
        break;
    }
    return QCoreApplication::translate("HikariSub.Automation", "Not a macro shortcut");
}

} // namespace

AutomationHotkeysController::AutomationHotkeysController(AutomationShell &automation, ui::SettingsStore &settings,
                                                         QObject *parent)
    : QObject(parent), m_automation(automation), m_settings(settings)
{
    load();
    m_installed = m_committed;
    connect(m_automation.managerController(), &ui::AutomationManagerController::changed, this, [this] {
        emit bindingsChanged();
        emit shortcutsChanged();
    });
}

void AutomationHotkeysController::load()
{
    for (const QString &row : m_settings.list(application::kAutomationHotkeysSetting.data())) {
        const QStringList v = row.split(QLatin1Char('\t'));
        application::MacroBinding b;
        b.legacyName = v.value(0).toStdString();
        b.keys = canonicalKeys(v.value(1).toStdString());
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

application::AliasResolution AutomationHotkeysController::resolution(const application::MacroBinding &binding) const
{
    const auto alias = application::aliasOfLegacyName(binding.legacyName);
    if (!alias) {
        application::AliasResolution none;
        none.problem = application::AliasProblem::MalformedAlias;
        return none;
    }
    std::optional<application::MacroIdentity> recorded;
    if (!binding.macroName.empty()) {
        application::MacroIdentity r;
        r.name = binding.macroName;
        r.scriptSha256 = binding.scriptSha256;
        recorded = r;
    }
    return m_automation.manager().registry().resolve(*alias, recorded);
}

std::optional<std::pair<std::string, int>>
AutomationHotkeysController::resolve(const application::MacroBinding &binding) const
{
    const auto r = resolution(binding);
    if (!r.identity)
        return std::nullopt;
    return std::pair(r.identity->scriptPath, r.identity->ordinal);
}

QString AutomationHotkeysController::scriptLabel(const std::string &path) const
{
    for (const auto &s : m_automation.manager().scripts())
        if (s.path == path && !s.info.name.empty())
            return qs(s.info.name);
    return fileNameOf(path);
}

QVariantMap AutomationHotkeysController::keys() const
{
    QVariantMap out;
    for (const auto &[name, b] : m_committed)
        if (resolve(b))
            out.insert(qs(name), qs(b.keys));
    return out;
}

QVariantList AutomationHotkeysController::missing() const
{
    QVariantList out;
    for (const auto &[name, b] : m_committed) {
        const auto r = resolution(b);
        if (r.identity)
            continue;
        out << QVariantMap{{QStringLiteral("legacyName"), qs(name)},
                           {QStringLiteral("macro"), qs(b.macroName)},
                           {QStringLiteral("keys"), qs(b.keys)},
                           {QStringLiteral("problem"), problemText(r.problem.value_or(application::AliasProblem::MissingScript))}};
    }
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

std::vector<std::string> AutomationHotkeysController::unboundMacros() const
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto &m : m_automation.manager().registry().macros()) {
        const std::string name = application::legacyNameOf(fileNameOf(m.scriptPath).toStdString(), m.ordinal);
        if (!m_committed.contains(name) && seen.insert(name).second && recordingProblem(qs(name)).isEmpty())
            out.push_back(name);
    }
    return out;
}

QVariantMap AutomationHotkeysController::describe(const std::string &legacyName) const
{
    const auto it = m_committed.find(legacyName);
    application::MacroBinding binding;
    if (it != m_committed.end())
        binding = it->second;
    else
        binding.legacyName = legacyName;
    const auto r = resolution(binding);
    if (r.identity)
        return {{QStringLiteral("script"), scriptLabel(r.identity->scriptPath)},
                {QStringLiteral("macro"), qs(r.identity->name)},
                {QStringLiteral("problem"), QString()}};
    const auto alias = application::aliasOfLegacyName(legacyName);
    const QString file = alias ? qs(alias->substr(0, alias->rfind(':'))) : qs(legacyName);
    return {{QStringLiteral("script"), file},
            {QStringLiteral("macro"), qs(binding.macroName)},
            {QStringLiteral("problem"), problemText(r.problem.value_or(application::AliasProblem::MissingScript))}};
}

std::map<std::string, std::string> AutomationHotkeysController::committedKeys() const
{
    std::map<std::string, std::string> out;
    for (const auto &[name, b] : m_committed)
        out[name] = b.keys;
    return out;
}

void AutomationHotkeysController::recordIdentity(application::MacroBinding &binding) const
{
    for (const auto &m : m_automation.manager().registry().macros())
        if (application::legacyNameOf(fileNameOf(m.scriptPath).toStdString(), m.ordinal) == binding.legacyName) {
            binding.macroName = m.name;
            binding.scriptSha256 = m.scriptSha256;
        }
}

void AutomationHotkeysController::replaceCommitted(const std::map<std::string, std::string> &keys)
{
    std::map<std::string, application::MacroBinding> next;
    for (const auto &[name, k] : keys) {
        if (k.empty())
            continue;
        const auto kept = m_committed.find(name);
        application::MacroBinding b;
        if (kept != m_committed.end())
            b = kept->second;
        else
            b.legacyName = name;
        if (kept == m_committed.end() || kept->second.keys != k)
            recordIdentity(b);
        b.keys = k;
        next[name] = std::move(b);
    }
    m_committed = std::move(next);
    emit bindingsChanged();
}

bool AutomationHotkeysController::renew(const QString &legacyName)
{
    const auto it = m_committed.find(legacyName.toStdString());
    if (it == m_committed.end())
        return false;
    application::MacroBinding b = it->second;
    recordIdentity(b);
    if (b.macroName == it->second.macroName && b.scriptSha256 == it->second.scriptSha256)
        return false;
    it->second = std::move(b);
    commitChange();
    return true;
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

QString AutomationHotkeysController::recordingProblem(const QString &legacyName) const
{
    application::MacroBinding binding;
    binding.legacyName = legacyName.toStdString();
    if (resolution(binding).problem != application::AliasProblem::BasenameCollision)
        return {};
    return QCoreApplication::translate("HikariSub.Automation",
                                       "Several loaded scripts have this file name, so a shortcut cannot tell their macros apart.");
}

void AutomationHotkeysController::commitChange()
{
    save();
    emit bindingsChanged();
    emit committed();
}

void AutomationHotkeysController::clearKeys(const QString &legacyName)
{
    if (m_committed.erase(legacyName.toStdString()) == 0)
        return;
    commitChange();
}

bool AutomationHotkeysController::run(const QString &legacyName)
{
    const auto it = m_committed.find(legacyName.toStdString());
    if (it == m_committed.end())
        return false;
    const auto target = resolve(it->second);
    return target && m_automation.run(target->first, target->second, AutomationShell::RunOrigin::Hotkey);
}

QVariantMap AutomationHotkeysController::importLegacy(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {{QStringLiteral("read"), false}};
    const QByteArray bytes = f.readAll();
    int added = 0;
    int kept = 0;
    QStringList skipped;
    for (auto &b : application::parseLegacyScriptHotkeys(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))) {
        if (m_committed.contains(b.legacyName)) {
            ++kept;
            continue;
        }
        b.keys = canonicalKeys(b.keys);
        bool taken = false;
        for (const auto &[name, existing] : m_committed)
            taken = taken || existing.keys == b.keys;
        if (taken) {
            skipped << QStringLiteral("%1 (%2)").arg(QString::fromStdString(b.legacyName), QString::fromStdString(b.keys));
            continue;
        }
        m_committed[b.legacyName] = std::move(b);
        ++added;
    }
    if (added > 0)
        commitChange();
    return {{QStringLiteral("read"), true},
            {QStringLiteral("added"), added},
            {QStringLiteral("kept"), kept},
            {QStringLiteral("skipped"), int(skipped.size())},
            {QStringLiteral("skippedNames"), skipped}};
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
