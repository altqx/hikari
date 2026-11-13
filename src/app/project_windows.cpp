#include "hikari/app/project_windows.h"

#include "hikari/app/session_json.h"
#include "hikari/application/owned_file.h"
#include "hikari/application/project.h"
#include "settings_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hikari::app {

bool recentPathMissing(const std::string &path, bool folder)
{
    return application::isMissingLocalFile(
        path,
        [folder](const std::string &p) {
            const QFileInfo info(QString::fromStdString(p));
            return folder ? info.isDir() : info.isFile();
        },
        [](char drive) {
#ifdef _WIN32
            const wchar_t root[] = {static_cast<wchar_t>(drive), L':', L'\\', 0};
            return ::GetDriveTypeW(root) == DRIVE_REMOTE;
#else
            (void)drive;
            return false;
#endif
        });
}

ProjectWindows::ProjectWindows(ui::SettingsStore &settings, QString userFolder, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_userFolder(std::move(userFolder))
{
    m_recent.set(m_settings.settings().list("recent.projects"));
    m_recent.setOpenedRecords(m_settings.settings().list("recent.projectsOpened"));
}

QString ProjectWindows::canonicalFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return {};
    const QString canonical = info.canonicalFilePath();
    return QDir::toNativeSeparators(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString ProjectWindows::root(int window) const
{
    const auto found = m_roots.find(window);
    return found == m_roots.end() ? QString() : found->second;
}

QStringList ProjectWindows::roots() const
{
    QStringList out;
    for (const auto &[window, root] : m_roots)
        if (!root.isEmpty())
            out << root;
    return out;
}

QString ProjectWindows::name(int window) const
{
    const QString folder = root(window);
    return folder.isEmpty() ? QString()
                            : QString::fromStdString(application::projectName(QDir::fromNativeSeparators(folder).toStdString()));
}

std::optional<int> ProjectWindows::windowOf(const QString &root) const
{
    for (const auto &[window, folder] : m_roots)
        if (folder == root)
            return window;
    return std::nullopt;
}

void ProjectWindows::set(int window, const QString &root)
{
    if (root.isEmpty()) {
        if (m_roots.erase(window) > 0)
            emit changed(window);
        return;
    }
    m_recent.add(root.toStdString(), QDateTime::currentMSecsSinceEpoch());
    storeRecent();
    if (this->root(window) == root)
        return;
    m_roots[window] = root;
    emit changed(window);
}

void ProjectWindows::forget(int window)
{
    m_roots.erase(window);
}

QVariantList ProjectWindows::recentRows() const
{
    QVariantList rows;
    for (const auto &entry : m_recent.entries()) {
        const QString path = QString::fromStdString(entry);
        const auto opened = m_recent.opened(entry);
        rows << QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), QString::fromStdString(application::projectName(QDir::fromNativeSeparators(path).toStdString()))},
            {QStringLiteral("opened"), opened ? QDateTime::fromMSecsSinceEpoch(*opened) : QDateTime()},
            {QStringLiteral("missing"), recentPathMissing(entry, true)}};
    }
    return rows;
}

void ProjectWindows::remove(const QString &path)
{
    if (m_recent.remove(path.toStdString()))
        storeRecent();
}

void ProjectWindows::removeMissing()
{
    if (m_recent.prune([](const std::string &path) { return recentPathMissing(path, true); }))
        storeRecent();
}

void ProjectWindows::storeRecent()
{
    m_settings.settings().set("recent.projects", m_recent.entries());
    m_settings.settings().set("recent.projectsOpened", m_recent.openedRecords());
    emit recentChanged();
}

QString ProjectWindows::tabsFile(const QString &root) const
{
    if (m_userFolder.isEmpty() || root.isEmpty())
        return {};
    return m_userFolder + QStringLiteral("/projects/") + QString::fromStdString(application::projectKey(root.toStdString())) +
           QStringLiteral(".json");
}

void ProjectWindows::keepTabs(const QString &root, application::Session session) const
{
    const QString file = tabsFile(root);
    if (file.isEmpty() || session.tabs.empty())
        return;
    if (session.windows.empty())
        session.windows.emplace_back();
    session.windows.front().project = root.toStdString();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(writeSessionJson(session));
        out.commit();
    }
}

std::optional<application::Session> ProjectWindows::lastTabs(const QString &root) const
{
    QFile in(tabsFile(root));
    if (!in.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = in.readAll();
    auto session = readSession(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!session || session->windows.empty() || session->windows.front().project != root.toStdString() || session->tabs.empty())
        return std::nullopt;
    return session;
}

}
