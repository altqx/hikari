#include "hikari/app/whats_new.h"

#include "hikari/app/package_kind.h"
#include "hikari/backends/desktop_handoff.h"
#include "settings_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

#include <utility>

namespace hikari::app {

namespace {

namespace a = application;

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

bool isWebLink(const QUrl &url)
{
    return url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) &&
           !url.host().isEmpty() && url.userInfo().isEmpty();
}

// One release of the list as the views take it.
QVariantMap shownRelease(const a::ReleaseEntry &release)
{
    const QString tag = qs(release.tag);
    const QString version = tag.mid(1);
    const QString name = qs(release.name);
    const QDateTime published = QDateTime::fromString(qs(release.published), Qt::ISODate);
    return {{QStringLiteral("version"), version},
            // The reader names a release without a name for its tag.
            {QStringLiteral("name"), name.isEmpty() || name == tag ? QStringLiteral("HikariSub ") + version : name},
            {QStringLiteral("date"), published.isValid() ? published.toLocalTime().date() : QDate()},
            {QStringLiteral("prerelease"), a::isPrerelease(release)},
            {QStringLiteral("notes"), qs(release.notes)}};
}

// The notes before md4c reads them, outside code (fences, `spans`):
// - raw HTML goes, tags and comments alike (with MarkdownNoHTML Qt would
//   show it as text); its text between tags stays, but a script's or a
//   style's goes with it;
// - a bare URL becomes an autolink ("<url>"), so all of it is the link:
//   md4c's own detection stops at "...", and GitHub's "Full Changelog"
//   lines read ".../compare/v0.1.2...v0.1.3". Not in a link's own syntax;
//   trailing punctuation stays outside, as on GitHub.
QString prepareMarkdown(const QString &markdown)
{
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][A-Za-z0-9-]*(\\s[^>]*)?/?>"));
    static const QRegularExpression url(
        QStringLiteral("(^|[^(<\\[\\w/\"'=])(https?://[^\\s<>`]*[^\\s<>`.,:;!?'\")\\]*_~])"));
    // What is dropped whole, up to its end, over lines too.
    static const QRegularExpression dropped(QStringLiteral("<!--|<(script|style)(?=[\\s>/])"),
                                            QRegularExpression::CaseInsensitiveOption);
    QStringList lines = markdown.split(QLatin1Char('\n'));
    bool fenced = false;
    QRegularExpression closer; // empty while nothing is being dropped
    for (QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (closer.pattern().isEmpty() &&
            (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~")))) {
            fenced = !fenced;
            continue;
        }
        if (fenced)
            continue;
        // Between backticks is code: every other part.
        QStringList parts = line.split(QLatin1Char('`'));
        for (qsizetype i = 0; i < parts.size(); i += 2) {
            QString &part = parts[i];
            QString kept;
            qsizetype at = 0;
            while (at < part.size()) {
                if (!closer.pattern().isEmpty()) {
                    const QRegularExpressionMatch end = closer.match(part, at);
                    if (!end.hasMatch())
                        break;
                    closer = QRegularExpression();
                    at = end.capturedEnd();
                    continue;
                }
                const QRegularExpressionMatch open = dropped.match(part, at);
                kept += part.mid(at, open.hasMatch() ? open.capturedStart() - at : -1);
                if (!open.hasMatch())
                    break;
                closer = open.captured(1).isEmpty()
                             ? QRegularExpression(QStringLiteral("-->"))
                             : QRegularExpression(QStringLiteral("</%1\\s*>").arg(open.captured(1)),
                                                  QRegularExpression::CaseInsensitiveOption);
                at = open.capturedEnd();
            }
            part = kept.remove(tag).replace(url, QStringLiteral("\\1<\\2>"));
        }
        line = parts.join(QLatin1Char('`'));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

WhatsNew::WhatsNew(ui::SettingsStore &settings, UpdateChecker &updates, QString cacheFile, QObject *parent)
    : QObject(parent), m_settings(settings), m_updates(updates), m_cacheFile(std::move(cacheFile))
{
    connect(&m_updates, &UpdateChecker::releasesRead, this, &WhatsNew::listRead);
    // The channel, and whether checks are on ("off" or "failed").
    connect(&m_updates, &UpdateChecker::optionsChanged, this, [this] {
        show();
        emit changed();
    });
}

void WhatsNew::start()
{
    const QString installed = m_updates.version();
    m_since = qs(a::whatsNewSince(m_settings.text("updater.lastVersion").toStdString(),
                                  m_settings.text("updater.notesSince").toStdString(), installed.toStdString()));
    m_settings.set("updater.lastVersion", installed);
    m_settings.set("updater.notesSince", m_since);
    m_known = loadCache();
    if (!m_known && m_updates.autoCheck())
        read();
    show();
    emit changed();
}

QString WhatsNew::state() const
{
    if (m_waiting)
        return QStringLiteral("fetching");
    if (m_known)
        return m_shown.isEmpty() ? QStringLiteral("none") : QStringLiteral("ready");
    return m_updates.autoCheck() ? QStringLiteral("failed") : QStringLiteral("off");
}

void WhatsNew::fetchIfNeeded()
{
    if (m_known || m_waiting || !m_updates.autoCheck())
        return;
    read();
    emit changed();
}

void WhatsNew::read()
{
    m_waiting = true;
    // A check that runs reads the list anyway; its releasesRead comes here.
    m_updates.readReleases();
}

void WhatsNew::listRead(bool ok)
{
    // Only while checks are on: a list a menu check read with them off is
    // not taken. A list read refreshes the notes (edited on GitHub since).
    if (!m_waiting && !(ok && m_updates.autoCheck()))
        return;
    m_waiting = false;
    if (ok) {
        m_between = a::whatsNewReleases(m_updates.releases(), m_since.toStdString(), m_updates.version().toStdString());
        m_known = true;
        saveCache();
    }
    show();
    emit changed();
}

void WhatsNew::show()
{
    m_shown.clear();
    m_more = 0;
    if (!m_known)
        return;
    const auto shown = a::whatsNewShown(m_between, m_updates.version().toStdString(), m_updates.stableOnlyApplied());
    for (const auto &release : shown.releases)
        m_shown.append(shownRelease(release));
    m_more = int(shown.more);
}

// {"installed", "since", "releases": [{tag, name, published, prerelease,
// notes}]}: the releases between the two, every channel's.
bool WhatsNew::loadCache()
{
    m_between.clear();
    if (m_cacheFile.isEmpty())
        return false;
    QFile file(m_cacheFile);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxCacheBytes)
        return false;
    const QJsonObject cache = QJsonDocument::fromJson(file.readAll()).object();
    if (cache.value(QStringLiteral("installed")).toString() != m_updates.version() ||
        !cache.value(QStringLiteral("since")).isString() || cache.value(QStringLiteral("since")).toString() != m_since ||
        !cache.value(QStringLiteral("releases")).isArray())
        return false;
    for (const QJsonValue &value : cache.value(QStringLiteral("releases")).toArray()) {
        const QJsonObject r = value.toObject();
        a::ReleaseEntry release;
        release.tag = r.value(QStringLiteral("tag")).toString().toStdString();
        release.name = r.value(QStringLiteral("name")).toString().toStdString();
        release.published = r.value(QStringLiteral("published")).toString().toStdString();
        release.prerelease = r.value(QStringLiteral("prerelease")).toBool();
        release.notes = r.value(QStringLiteral("notes")).toString().toStdString();
        m_between.push_back(std::move(release));
    }
    // What was written for this range, kept to its rules.
    m_between = a::whatsNewReleases(m_between, m_since.toStdString(), m_updates.version().toStdString());
    return true;
}

QByteArray WhatsNew::cacheContent(const QString &installed, const QString &since,
                                  const std::vector<a::ReleaseEntry> &releases, qint64 limit)
{
    QJsonArray entries;
    for (const auto &release : releases)
        entries.append(QJsonObject{{QStringLiteral("tag"), qs(release.tag)},
                                   {QStringLiteral("name"), qs(release.name)},
                                   {QStringLiteral("published"), qs(release.published)},
                                   {QStringLiteral("prerelease"), release.prerelease},
                                   {QStringLiteral("notes"), qs(release.notes)}});
    const auto content = [&] {
        return QJsonDocument(QJsonObject{{QStringLiteral("installed"), installed},
                                         {QStringLiteral("since"), since},
                                         {QStringLiteral("releases"), entries}})
            .toJson(QJsonDocument::Compact);
    };
    QByteArray bytes = content();
    while (bytes.size() > limit && !entries.isEmpty()) {
        // Each entry's share is itself and its comma: the lowest go until
        // that much is gone, then the content is measured again.
        qint64 over = bytes.size() - limit;
        while (over > 0 && !entries.isEmpty()) {
            over -= QJsonDocument(entries.last().toObject()).toJson(QJsonDocument::Compact).size() + 1;
            entries.removeLast();
        }
        bytes = content();
    }
    return bytes.size() <= limit ? bytes : QByteArray();
}

void WhatsNew::saveCache() const
{
    if (m_cacheFile.isEmpty())
        return;
    const QByteArray content = cacheContent(m_updates.version(), m_since, m_between, kMaxCacheBytes);
    if (content.isEmpty()) {
        QFile::remove(m_cacheFile);
        return;
    }
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile file(m_cacheFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
        file.commit();
    }
}

QString WhatsNew::render(const QString &markdown, const QColor &muted, const QColor &link, const QColor &code) const
{
    QTextDocument doc;
    doc.setMarkdown(prepareMarkdown(markdown), QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub |
                                                              QTextDocument::MarkdownNoHTML));
    // Images first, last to first so the positions hold: the alt text in
    // their place, nothing loaded.
    struct Image {
        int position, length;
        QString alt;
    };
    std::vector<Image> images;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (const QTextFragment fragment = it.fragment(); fragment.charFormat().isImageFormat())
                images.push_back({fragment.position(), fragment.length(),
                                  fragment.charFormat().property(QTextFormat::ImageAltText).toString()});
    for (auto it = images.rbegin(); it != images.rend(); ++it) {
        QTextCursor cursor(&doc);
        cursor.setPosition(it->position);
        cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
        cursor.insertText(it->alt, QTextCharFormat());
    }
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        // A heading is a section label: the body's size, semibold, muted.
        if (block.blockFormat().headingLevel() > 0) {
            QTextCursor cursor(block);
            QTextBlockFormat format = block.blockFormat();
            format.setHeadingLevel(0);
            cursor.setBlockFormat(format);
            cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            QTextCharFormat label;
            label.setFontWeight(QFont::DemiBold);
            label.setProperty(QTextFormat::FontSizeAdjustment, 0);
            label.setForeground(muted);
            cursor.mergeCharFormat(label);
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat format = fragment.charFormat();
            bool changed = false;
            if (format.isAnchor()) {
                if (isWebLink(QUrl(format.anchorHref(), QUrl::StrictMode))) {
                    format.setForeground(link);
                    format.setFontUnderline(true);
                } else {
                    format.setAnchor(false);
                    format.setAnchorHref(QString());
                    format.clearForeground();
                    format.setFontUnderline(false);
                }
                changed = true;
            }
            // Code at the view's size, on its own background.
            if (format.fontFixedPitch() || format.hasProperty(QTextFormat::FontFamilies) ||
                format.hasProperty(QTextFormat::FontPointSize)) {
                format.clearProperty(QTextFormat::FontPointSize);
                format.clearProperty(QTextFormat::FontPixelSize);
                if (format.fontFixedPitch())
                    format.setBackground(code);
                changed = true;
            }
            if (changed) {
                QTextCursor cursor(&doc);
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                cursor.setCharFormat(format);
            }
        }
    }
    // The document's font stays out: the view's applies.
    static const QRegularExpression body(QStringLiteral("<body[^>]*>"));
    return doc.toHtml().replace(body, QStringLiteral("<body>"));
}

bool WhatsNew::openLink(const QString &url)
{
    const QUrl target(url, QUrl::StrictMode);
    if (!isWebLink(target))
        return false;
    return backends::desktop::openUrl(target);
}

void WhatsNew::openAllReleaseNotes()
{
    backends::desktop::openUrl(allReleaseNotesUrl(m_updates.feed()));
}

QUrl WhatsNew::allReleaseNotesUrl(const UpdateFeed &feed)
{
    QString page = feed.pageBase; // ".../releases/tag/"
    if (page.endsWith(QLatin1String("/tag/")))
        page.chop(5);
    return QUrl(page);
}

QString WhatsNew::cacheFileIn(const UpdateEnvironment &environment)
{
    if (environment.cacheDir.isEmpty())
        return {};
    const auto key = a::updateCopyKey(packageKindToken(environment.package.kind).toStdString(),
                                      environment.package.appImage.toStdString());
    return environment.cacheDir + QStringLiteral("/whats-new-%1.json").arg(qs(key));
}

} // namespace hikari::app
