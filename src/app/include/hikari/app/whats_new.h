#pragma once

#include "hikari/app/update_checker.h"
#include "hikari/application/update_check.h"

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <vector>

namespace hikari::app {

class WhatsNew : public QObject {
    Q_OBJECT
    // Highest first: {version, name, date (QDate, or invalid), prerelease, notes (Markdown)}.
    Q_PROPERTY(QVariantList releases READ releases NOTIFY changed)
    // How many releases past the ten shown are left out.
    Q_PROPERTY(int more READ more NOTIFY changed)
    // The version run before this one; empty on a first start.
    Q_PROPERTY(QString since READ since NOTIFY changed)
    // "ready" (notes to show), "none" (read, none to show), "fetching",
    // "failed" (not read, none cached) or "off" (checks off, none cached).
    Q_PROPERTY(QString state READ state NOTIFY changed)
public:
    // cacheFile empty: in memory only, so every start is a first one.
    WhatsNew(UpdateChecker &updates, QString cacheFile, QObject *parent = nullptr);

    // Called once, after the update check has started.
    void start();

    QVariantList releases() const { return m_shown; }
    int more() const { return m_more; }
    QString since() const { return m_since; }
    QString state() const;

    // Reads the list only when nothing is cached and update checks are on.
    Q_INVOKABLE void fetchIfNeeded();
    // GitHub-flavoured Markdown, no raw HTML: headings in muted, http(s) links in link, images as
    // their alt text (never loaded), code on code. The view's font applies.
    Q_INVOKABLE QString render(const QString &markdown, const QColor &muted, const QColor &link,
                               const QColor &code) const;
    // http(s) links only.
    Q_INVOKABLE bool openLink(const QString &url);
    Q_INVOKABLE void openAllReleaseNotes();
    static QUrl allReleaseNotesUrl(const UpdateFeed &feed);
    // One cache file per copy, named by the update installer's copy key.
    static QString cacheFileIn(const UpdateEnvironment &environment);
    static constexpr qint64 kMaxCacheBytes = 4 * 1024 * 1024;
    // At most limit bytes: past it the lowest versions' notes go, first those neither channel
    // shows among its ten, then the lowest versions; empty when nothing fits.
    static QByteArray cacheContent(const QString &installed, const QString &since,
                                   const std::vector<application::ReleaseEntry> &releases, qint64 limit);

signals:
    void changed();

private:
    void read();
    void listRead(bool ok);
    void show();
    QJsonObject readCache() const;
    bool loadCache(const QJsonObject &cache);
    void saveCache() const;

    UpdateChecker &m_updates;
    QString m_cacheFile;
    QString m_since;
    std::vector<application::ReleaseEntry> m_between;
    bool m_known = false;
    bool m_waiting = false;
    bool m_started = false;
    QVariantList m_shown;
    int m_more = 0;
};

} // namespace hikari::app
