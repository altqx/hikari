#include "hikari/app/application.h"

#include "hikari/app/ai_features.h"
#include "hikari/app/window_settings.h"
#include "hikari/application/ai_project_keys.h"
#include "hikari/application/ai_providers.h"
#include "hikari/backends/desktop_handoff.h"
#include "project_credentials.h"
#include "project_file.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <set>
#include <string>

namespace hikari::app {

namespace {

namespace ai = application::ai;

QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

ai::EnvironmentLookup processEnvironment()
{
    return [](std::string_view name) -> std::optional<std::string> {
        const QString value = qEnvironmentVariable(std::string(name).c_str());
        if (value.isEmpty())
            return std::nullopt;
        return value.toStdString();
    };
}

std::optional<ai::ProjectKey> foundKey(const WindowSettings &window, std::string_view providerId)
{
    const auto *project = window.project();
    if (!project)
        return std::nullopt;
    const auto names = project->aiKeyNames();
    const auto name = names.byProvider.find(providerId);
    if (name == names.byProvider.end())
        return std::nullopt;
    return ai::resolveProjectKey(name->second, processEnvironment(), ui::ProjectCredentials(window.root()).keys());
}

bool hasFoundKeys(const WindowSettings &window)
{
    const auto *project = window.project();
    if (!project)
        return false;
    for (const auto &[provider, name] : project->aiKeyNames().byProvider)
        if (foundKey(window, provider))
            return true;
    return false;
}

QString usedState(const WindowSettings &window, const ai::KeyName *name, const std::optional<ai::ProjectKey> &key)
{
    if (!name)
        return QStringLiteral("yours");
    if (!window.trusted())
        return QStringLiteral("untrusted");
    if (key)
        return key->source == ai::KeySource::Environment ? QStringLiteral("env") : QStringLiteral("file");
    if (!name->entry.empty() && !ui::ProjectCredentials(window.root()).exists())
        return QStringLiteral("noFile");
    return QStringLiteral("notFound");
}

}

std::optional<std::string> Application::projectAiKey(const QString &root, std::string_view providerId) const
{
    for (const int shellWindow : sessionWindows()) {
        const auto &window = *views(shellWindow).settings;
        if (window.root() == root && window.trusted())
            if (auto key = foundKey(window, providerId))
                return std::move(key->key);
    }
    return std::nullopt;
}

QStringList Application::projectTrustReasons(int shellWindow) const
{
    const auto &window = *views(shellWindow).settings;
    const auto *project = window.project();
    if (!project || window.trusted())
        return {};
    QStringList reasons;
    if (QFileInfo(window.root() + QStringLiteral("/.hikari/automation")).isDir() || project->hasTable("plugins"))
        reasons << QStringLiteral("scripts");
    if (hasFoundKeys(window))
        reasons << QStringLiteral("keys");
    return reasons;
}

QVariantMap Application::projectAiKeys(int shellWindow) const
{
    const auto &window = *views(shellWindow).settings;
    const auto *project = window.project();
    if (!project)
        return {};
    const auto names = project->aiKeyNames();
    std::set<std::string, std::less<>> listed;
    QVariantList rows;
    const auto row = [&](const std::string &id, const QString &label, bool needsKey) {
        listed.insert(id);
        const auto name = names.byProvider.find(id);
        const ai::KeyName *named = name == names.byProvider.end() ? nullptr : &name->second;
        const auto key = named ? foundKey(window, id) : std::nullopt;
        rows << QVariantMap{{QStringLiteral("id"), qs(id)},
                            {QStringLiteral("name"), label},
                            {QStringLiteral("needsKey"), needsKey},
                            {QStringLiteral("known"), !label.isEmpty()},
                            {QStringLiteral("env"), named ? qs(named->env) : QString()},
                            {QStringLiteral("entry"), named ? qs(named->entry) : QString()},
                            {QStringLiteral("used"), needsKey ? usedState(window, named, key) : QStringLiteral("noKey")},
                            {QStringLiteral("from"), key ? qs(key->from) : QString()}};
    };
    if (const auto *providers = m_ai ? m_ai->providers() : nullptr)
        for (const auto &provider : providers->config().providers)
            row(provider.id, qs(provider.name.empty() ? provider.id : provider.name), provider.needsKey);
    for (const auto &[id, name] : names.byProvider)
        if (!listed.contains(id))
            row(id, {}, true);
    QStringList valuesFound;
    for (const auto &id : names.valuesFound)
        valuesFound << qs(id);
    const ui::ProjectCredentials credentials(window.root());
    return {{QStringLiteral("trusted"), window.trusted()},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("valuesFound"), valuesFound},
            {QStringLiteral("credentialsExists"), credentials.exists()},
            {QStringLiteral("credentialsPath"), QDir::toNativeSeparators(credentials.path())}};
}

QStringList Application::projectKeyValuesFound(int shellWindow) const
{
    const auto *project = views(shellWindow).settings->project();
    QStringList out;
    if (project)
        for (const auto &id : project->aiKeyNames().valuesFound)
            out << qs(id);
    return out;
}

bool Application::applyProjectAiKeys(int shellWindow, const QVariantMap &names)
{
    auto *project = views(shellWindow).settings->project();
    if (!project)
        return false;
    const auto current = project->aiKeyNames();
    bool ok = true;
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        const QVariantMap fields = it.value().toMap();
        const ai::KeyName name{fields.value(QStringLiteral("env")).toString().trimmed().toStdString(),
                               fields.value(QStringLiteral("entry")).toString().trimmed().toStdString()};
        const std::string id = it.key().toStdString();
        const auto known = current.byProvider.find(id);
        if (known == current.byProvider.end() ? name.empty() : known->second == name)
            continue;
        ok = project->setAiKeyName(id, name) && ok;
    }
    applyWindowProject(shellWindow);
    emit projectsChanged();
    return ok;
}

bool Application::createProjectCredentials(int shellWindow)
{
    const auto &window = *views(shellWindow).settings;
    const auto *project = window.project();
    if (!project)
        return false;
    std::vector<std::string> entries;
    for (const auto &[provider, name] : project->aiKeyNames().byProvider)
        if (!name.entry.empty())
            entries.push_back(name.entry);
    const bool ok = ui::ProjectCredentials(window.root()).create(entries);
    emit projectsChanged();
    return ok;
}

bool Application::moveProjectKeysToCredentials(int shellWindow)
{
    const auto &window = *views(shellWindow).settings;
    auto *project = window.project();
    if (!project)
        return false;
    const ui::ProjectCredentials credentials(window.root());
    bool ok = true;
    for (const auto &value : project->aiKeyValues()) {
        if (!credentials.put(value.provider, value.key)) {
            ok = false;
            continue;
        }
        ok = (value.inKeysTable ? project->removeKeysEntry(value.provider)
                                : project->setAiKeyName(value.provider, {{}, value.provider})) &&
             ok;
    }
    applyWindowProject(shellWindow);
    emit projectsChanged();
    return ok;
}

void Application::openProjectCredentials(int shellWindow)
{
    const ui::ProjectCredentials credentials(views(shellWindow).settings->root());
    if (credentials.exists())
        backends::desktop::openUrl(QUrl::fromLocalFile(credentials.path()));
}

}
