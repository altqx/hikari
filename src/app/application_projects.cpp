#include "hikari/app/application.h"

#include "hikari/app/font_catalogs_controller.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/project.h"
#include "log_controller.h"
#include "project_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QQuickWindow>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace hikari::app {

namespace {

QString projectCredentialNotice(const std::string &id)
{
    return QCoreApplication::translate("HikariSub.Project",
                                       "%1 in project.toml looks like a credential, so it is ignored. Keys go in this "
                                       "Project’s credentials file (.hikari/credentials.toml) or this machine’s "
                                       "environment, never in project.toml.")
        .arg(QString::fromStdString(id));
}

}

std::optional<int> Application::openWindowBeside(int shellWindow)
{
    QQuickWindow *from = shellQuickWindow(shellWindow);
    QQmlEngine *engine = from ? qmlEngine(from) : nullptr;
    QQuickWindow *opened = engine ? openShellWindow(*engine) : nullptr;
    return opened ? std::optional(opened->property("shellWindow").toInt()) : std::nullopt;
}

bool Application::projectTrusted(const QString &root) const
{
#ifdef _WIN32
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    std::vector<std::string> trusted;
    for (const QString &folder : m_settings->list("trust.folders"))
        trusted.push_back(QDir::toNativeSeparators(folder).toStdString());
    return application::folderTrusted(root.toStdString(), trusted, windows);
}

application::Settings &Application::windowSettings(int shellWindow)
{
    return views(shellWindow).settings->settings();
}

const application::Settings &Application::windowSettings(int shellWindow) const
{
    return views(shellWindow).settings->settings();
}

std::optional<api::ProjectState> Application::activeProject() const
{
    const QString root = m_projects ? m_projects->root(m_workspace.activeWindow()) : QString();
    if (root.isEmpty())
        return std::nullopt;
    return api::ProjectState{m_projects->name(m_workspace.activeWindow()).toStdString(), root.toStdString(),
                             projectTrusted(root)};
}

void Application::applyWindowProject(int shellWindow)
{
    if (shellWindow != 0 && !m_shellWindows.contains(shellWindow)) {
        m_projectFonts.erase(shellWindow);
        projectScriptsAndPluginsChanged();
        return;
    }
    auto &v = views(shellWindow);
    const QString root = m_projects->root(shellWindow);
    v.settings->setProject(root, !root.isEmpty() && projectTrusted(root));
    projectScriptsAndPluginsChanged();
    if (const auto *project = v.settings->project()) {
        const QString title = QCoreApplication::translate("HikariSub.Project", "Project settings");
        for (const auto &problem : project->problems())
            m_log->notice(ui::LogController::Warning, title,
                          problem.keptAs.empty()
                              ? QCoreApplication::translate("HikariSub.Project",
                                                            "%1 could not be read, so its settings don’t apply.")
                                    .arg(QString::fromStdString(application::displayPath(problem.file)))
                              : QCoreApplication::translate("HikariSub.Project",
                                                            "%1 could not be read, so its settings don’t apply. It is "
                                                            "kept as %2.")
                                    .arg(QString::fromStdString(application::displayPath(problem.file)),
                                         QString::fromStdString(application::displayPath(problem.keptAs))),
                          QString::fromStdString(problem.failure.message), shellWindow);
        for (const auto &id : project->refused())
            m_log->notice(ui::LogController::Warning, title,
                          QCoreApplication::translate("HikariSub.Project",
                                                      "A Project can’t set %1, so the project file’s value is ignored.")
                              .arg(QString::fromStdString(id)),
                          {}, shellWindow);
        for (const auto &id : project->credentials())
            m_log->notice(ui::LogController::Warning, title,
                          projectCredentialNotice(id),
                          {}, shellWindow);
    }
    loadProjectFonts(shellWindow);
    applyProjectTierFiles(shellWindow, root);
    if (m_styleManager)
        m_styleManager->projectChanged();
    if (m_fontCatalogs)
        m_fontCatalogs->projectChanged();
    const bool moveTags = v.settings->settings().boolean("translation.autoMoveTagsFromOriginal");
    v.editor->setMoveTags(moveTags);
    v.shell->setRunMarks(moveTags);
    if (shellWindow == m_workspace.activeWindow())
        applyGrammarSettings();
    spellingRefresh();
    if (m_apiHost)
        m_apiHost->observe();
}

std::map<int, std::optional<plugins::ProjectPlugins>> Application::windowPlugins() const
{
    std::map<int, std::optional<plugins::ProjectPlugins>> windows;
    for (const int window : sessionWindows()) {
        const auto &settings = *views(window).settings;
        const auto *project = settings.trusted() ? settings.project() : nullptr;
        auto &entry = windows[window];
        if (!project || !project->hasTable("plugins"))
            continue;
        const auto table = project->plugins();
        plugins::ProjectPlugins plugins{table.enabled, table.disabled, {}};
        for (const auto &[id, json] : table.settingsJson)
            if (auto parsed = api::Json::parse(json))
                plugins.settings.emplace(id, std::move(*parsed));
        entry = std::move(plugins);
    }
    return windows;
}

void Application::projectScriptsAndPluginsChanged()
{
    if (m_automation)
        m_automation->projectsChanged();
    if (m_pluginRuntime)
        m_pluginRuntime->projectsChanged();
}

void Application::loadProjectFonts(int shellWindow)
{
    const auto *project = views(shellWindow).settings->project();
    std::vector<QString> folders = project ? project->fontFolders() : std::vector<QString>();
    const auto known = m_projectFonts.find(shellWindow);
    if (known == m_projectFonts.end() ? folders.empty() : known->second.folders == folders)
        return;
    ProjectFonts loaded{folders, {}};
    for (const QString &folder : folders) {
        auto fonts = FontCatalogsController::folderFontLeases(folder);
        if (!fonts) {
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Project", "Project settings"),
                          QCoreApplication::translate("HikariSub.Project", "The project font folder %1 can’t be opened.")
                              .arg(QDir::toNativeSeparators(folder)));
            continue;
        }
        std::ranges::move(*fonts, std::back_inserter(loaded.fonts));
    }
    if (folders.empty())
        m_projectFonts.erase(shellWindow);
    else
        m_projectFonts[shellWindow] = std::move(loaded);
    subtitleFontsChanged();
}

QString Application::projectFontFolder(int shellWindow) const
{
    const auto found = m_projectFonts.find(shellWindow);
    return found == m_projectFonts.end() || found->second.folders.empty() ? QString()
                                                                           : QDir::toNativeSeparators(found->second.folders.front());
}

bool Application::projectNeedsTrust(int shellWindow) const
{
    return !projectTrustReasons(shellWindow).isEmpty();
}

void Application::trustProject(int shellWindow)
{
    const QString root = views(shellWindow).settings->root();
    if (root.isEmpty())
        return;
    QStringList folders = m_settings->list("trust.folders");
    const QString folder = QDir::toNativeSeparators(root);
    if (!folders.contains(folder))
        m_settings->setValue(QStringLiteral("trust.folders"), folders << folder);
    m_settings->sync();
    emit projectsChanged();
}

QVariantMap Application::projectPlugins(int shellWindow) const
{
    const auto &settings = *views(shellWindow).settings;
    const auto *project = settings.project();
    if (!project)
        return {};
    const auto table = project->plugins();
    QStringList named;
    const auto name = [&](const std::string &id) {
        if (!named.contains(QString::fromStdString(id)))
            named << QString::fromStdString(id);
    };
    std::ranges::for_each(table.enabled, name);
    std::ranges::for_each(table.disabled, name);
    for (const auto &[id, json] : table.settingsJson)
        name(id);
    return {{QStringLiteral("trusted"), settings.trusted()},
            {QStringLiteral("named"), named},
            {QStringLiteral("missing"), m_pluginRuntime ? m_pluginRuntime->missingIn(shellWindow) : QStringList()}};
}

bool Application::windowEmpty(int shellWindow) const
{
    if (!m_projects->root(shellWindow).isEmpty())
        return false;
    if (windowHome(shellWindow))
        return true;
    return m_workspace.activeWindow() == shellWindow && m_workspace.tabs(shellWindow).size() == 1 && targetUntouched();
}

void Application::offerProjectFor(application::DocumentId document, const QString &path)
{
    const int window = m_workspace.window(document).value_or(m_workspace.activeWindow());
    if (!m_projects->root(window).isEmpty())
        return;
    const auto found =
        application::nearestProjectRoot(std::filesystem::path(QFileInfo(path).absoluteFilePath().toStdU16String()));
    const QString root = found ? ProjectWindows::canonicalFolder(QString::fromStdU16String(found->u16string())) : QString();
    if (root.isEmpty() || m_projects->windowOf(root))
        return;
    if (projectTrusted(root))
        m_projects->set(window, root);
    else
        emit projectOffered(window, root, QString::fromStdString(application::projectName(*found)));
}

QVariantMap Application::openFolder(const QString &path, int shellWindow, int request)
{
    const QString root = ProjectWindows::canonicalFolder(path);
    if (root.isEmpty()) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Project", "Open folder"),
                      QCoreApplication::translate("HikariSub.Project", "%1 is not a folder.")
                          .arg(QDir::toNativeSeparators(path)));
        return {{QStringLiteral("ok"), false}};
    }
    const auto open = m_projects->windowOf(root);
    int window = shellWindow;
    switch (application::placeFolder(open.has_value(), windowEmpty(shellWindow),
                                     static_cast<application::WindowRequest>(request))) {
    case application::FolderPlace::Focus:
        window = *open;
        break;
    case application::FolderPlace::Current:
        m_projects->set(shellWindow, root);
        break;
    case application::FolderPlace::NewWindow:
        if (const auto opened = openWindowBeside(shellWindow))
            window = *opened;
        else
            return {{QStringLiteral("ok"), false}};
        m_projects->set(window, root);
        break;
    }
    if (QQuickWindow *shown = shellQuickWindow(window)) {
        shown->raise();
        shown->requestActivate();
    }
    activateShellWindow(window);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("window"), window}};
}

QVariantList Application::reviewCloseProject(int shellWindow)
{
    if (m_projects->root(shellWindow).isEmpty())
        return {};
    activateShellWindow(shellWindow);
    m_projectClosing = shellWindow;
    return reviewClose(QStringLiteral("project"));
}

void Application::closeProject(int shellWindow)
{
    keepProjectTabs(shellWindow);
    for (const auto id : m_workspace.tabs(shellWindow))
        closeDocument(id);
    m_projects->set(shellWindow, {});
}

QVariantMap Application::projectLastTabs(int shellWindow) const
{
    const QString root = m_projects->root(shellWindow);
    const auto session = root.isEmpty() || !m_workspace.tabs(shellWindow).empty() ? std::nullopt : m_projects->lastTabs(root);
    const QStringList names = sessionNames(session);
    return {{QStringLiteral("count"), session ? static_cast<int>(session->tabs.size()) : 0},
            {QStringLiteral("names"), names}};
}

bool Application::reopenProjectTabs(int shellWindow)
{
    const QString root = m_projects->root(shellWindow);
    if (root.isEmpty() || !m_workspace.tabs(shellWindow).empty() || m_pendingSession)
        return false;
    auto session = m_projects->lastTabs(root);
    if (!session)
        return false;
    activateShellWindow(shellWindow);
    PendingSession pending = stageSession(std::move(*session));
    pending.intoWindow = shellWindow;
    m_pendingSession = std::move(pending);
    applySession();
    return true;
}

QString Application::projectMoveText(int shellWindow) const
{
    const QString name = m_projects->name(shellWindow);
    return name.isEmpty() ? QString()
                          : QCoreApplication::translate("HikariSub.Project", "This tab now uses the project %1.").arg(name);
}

}
