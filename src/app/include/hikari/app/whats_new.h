#pragma once

// SH3 (#223, owner-approved 2026-10-21): What's new from the GitHub release
// notes, for the Home page's card and Help > What's new (no popup after an
// update). At each start it records the version that ran before this one
// (updater.lastVersion, updater.notesSince). While "Check for updates
// automatically" is on it reads the release list once per installed version
// (UpdateChecker::readReleases, or the list the start's own check reads),
// and keeps the notes after that version up to the installed one in a cache
// file; offline, or with checks off later, the cached notes still show. What
// shows follows the update checker's channel (whatsNewShown). The notes are
// Markdown, rendered as plain rich text by render().

#include "hikari/app/update_checker.h"
#include "hikari/application/update_check.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class WhatsNew : public QObject {
    Q_OBJECT
    // The releases shown, highest first: {version, name, date (QDate, or
    // invalid), prerelease, notes (Markdown)}.
    Q_PROPERTY(QVariantList releases READ releases NOTIFY changed)
    // How many more the at-most-ten leave out.
    Q_PROPERTY(int more READ more NOTIFY changed)
    // The version run before this one; empty on a first start.
    Q_PROPERTY(QString since READ since NOTIFY changed)
    // "ready" (notes to show), "none" (read, none to show), "fetching",
    // "failed" (not read, none cached) or "off" (checks off, none cached).
    Q_PROPERTY(QString state READ state NOTIFY changed)
public:
    // The notes are cached in `cacheFile`; empty: kept in memory only.
    WhatsNew(ui::SettingsStore &settings, UpdateChecker &updates, QString cacheFile, QObject *parent = nullptr);

    // At start, after the update check's: records the version, takes the
    // cache, reads the list when it is due.
    void start();

    QVariantList releases() const { return m_shown; }
    int more() const { return m_more; }
    QString since() const { return m_since; }
    QString state() const;

    // Help > What's new: reads the list when nothing is cached and update
    // checks are on.
    Q_INVOKABLE void fetchIfNeeded();
    // Release notes as rich text: GitHub-flavoured Markdown without raw
    // HTML; headings as section labels in `muted`; links in `link`, only
    // http(s) ones; an image as its alt text, never loaded; code on `code`.
    // The text takes the font of the view it is shown in.
    Q_INVOKABLE QString render(const QString &markdown, const QColor &muted, const QColor &link,
                               const QColor &code) const;
    // Opens a link from the notes in the browser: http(s) only.
    Q_INVOKABLE bool openLink(const QString &url);
    // "All release notes": the feed's releases page.
    Q_INVOKABLE void openAllReleaseNotes();
    static QUrl allReleaseNotesUrl(const UpdateFeed &feed);
    // The cache file's name in the update cache, per copy (the update
    // installer's copy key).
    static QString cacheFileIn(const UpdateEnvironment &environment);

signals:
    void changed();

private:
    void read();
    void listRead(bool ok);
    void show();
    bool loadCache();
    void saveCache() const;

    ui::SettingsStore &m_settings;
    UpdateChecker &m_updates;
    QString m_cacheFile;
    QString m_since;
    // The releases between m_since and the installed version, every
    // channel's, once read or taken from the cache.
    std::vector<application::ReleaseEntry> m_between;
    bool m_known = false;
    bool m_waiting = false;
    QVariantList m_shown;
    int m_more = 0;
};

} // namespace hikari::app
