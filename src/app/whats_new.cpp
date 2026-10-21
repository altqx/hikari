#include "hikari/app/whats_new.h"

#include "hikari/app/package_kind.h"
#include "hikari/backends/desktop_handoff.h"

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

#include <algorithm>
#include <utility>
#include <vector>

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

// md4c's flags (Qt passes MarkdownFeatures to it as they are): GitHub's
// permissive URL and www. autolinks stop at "...", and GitHub's "Full
// Changelog" lines read ".../compare/v0.1.2...v0.1.3"; linkUrls() links
// bare URLs instead.
constexpr int kMd4cPermissiveUrlAutolinks = 0x0004;
constexpr int kMd4cPermissiveWwwAutolinks = 0x0400;

// Code as md4c read it: a code block (fenced, in a list or a quote too,
// or indented) or a code span.
bool isCode(const QTextBlock &block, const QTextCharFormat &format)
{
    return block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage) ||
           block.blockFormat().hasProperty(QTextFormat::BlockCodeFence) || format.fontFixedPitch();
}

// Raw HTML goes, outside code (with MarkdownNoHTML md4c keeps it as text):
// tags (a quoted value may hold ">"), with their text between them kept; comments, scripts and styles
// whole, over code and paragraphs too. A paragraph left empty goes. An
// entity ("&lt;b&gt;") reads as what it stands for, so it goes too.
void dropHtml(QTextDocument &doc)
{
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][A-Za-z0-9-]*(\\s(?:[^>\"']|\"[^\"]*\"|'[^']*')*)?/?>"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression dropped(QStringLiteral("<!--|<(script|style)(?=[\\s>/]|$)"),
                                            QRegularExpression::CaseInsensitiveOption |
                                                QRegularExpression::UseUnicodePropertiesOption);
    struct Range {
        int from, to;
    };
    std::vector<Range> gone;
    const auto drop = [&](int from, int to) {
        if (from >= to)
            return;
        if (!gone.empty() && gone.back().to >= from)
            gone.back().to = std::max(gone.back().to, to);
        else
            gone.push_back({from, to});
    };
    QRegularExpression closer; // empty while nothing is being dropped
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        // The block's text in runs of code and not code.
        struct Run {
            int position;
            QString text;
            bool code;
        };
        std::vector<Run> runs;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const bool code = isCode(block, fragment.charFormat());
            if (!runs.empty() && runs.back().code == code && !code)
                runs.back().text += fragment.text();
            else
                runs.push_back({fragment.position(), fragment.text(), code});
        }
        const int before = int(gone.size());
        for (const Run &run : runs) {
            if (run.code) {
                if (!closer.pattern().isEmpty())
                    drop(run.position, run.position + int(run.text.size()));
                continue;
            }
            qsizetype at = 0;
            while (at < run.text.size()) {
                if (!closer.pattern().isEmpty()) {
                    const QRegularExpressionMatch end = closer.match(run.text, at);
                    const qsizetype to = end.hasMatch() ? end.capturedEnd() : run.text.size();
                    drop(run.position + int(at), run.position + int(to));
                    if (end.hasMatch())
                        closer = QRegularExpression();
                    at = to;
                    continue;
                }
                const QRegularExpressionMatch open = dropped.match(run.text, at);
                const QRegularExpressionMatch element = tag.match(run.text, at);
                if (open.hasMatch() && (!element.hasMatch() || open.capturedStart() <= element.capturedStart())) {
                    drop(run.position + int(open.capturedStart()), run.position + int(open.capturedEnd()));
                    closer = open.captured(1).isEmpty()
                                 ? QRegularExpression(QStringLiteral("-->"))
                                 : QRegularExpression(QStringLiteral("</%1\\s*>").arg(open.captured(1)),
                                                      QRegularExpression::CaseInsensitiveOption |
                                                          QRegularExpression::UseUnicodePropertiesOption);
                    at = open.capturedEnd();
                } else if (element.hasMatch()) {
                    drop(run.position + int(element.capturedStart()), run.position + int(element.capturedEnd()));
                    at = element.capturedEnd();
                } else {
                    break;
                }
            }
        }
        // Dropping on past the block takes its end too: the paragraphs join.
        if (!closer.pattern().isEmpty() && block.next().isValid())
            drop(block.position() + block.length() - 1, block.position() + block.length());
        if (int(gone.size()) != before || (!gone.empty() && gone.back().to > block.position()))
            block.setUserState(1); // touched: if it is left empty, it goes
    }
    for (auto it = gone.rbegin(); it != gone.rend(); ++it) {
        QTextCursor cursor(&doc);
        cursor.setPosition(it->from);
        cursor.setPosition(it->to, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    std::vector<QTextBlock> empty;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next())
        if (block.userState() == 1 && block.text().trimmed().isEmpty() && !isCode(block, QTextCharFormat()))
            empty.push_back(block);
    for (auto it = empty.rbegin(); it != empty.rend(); ++it) {
        QTextCursor cursor(&doc);
        if (it->next().isValid()) {
            cursor.setPosition(it->position());
            cursor.setPosition(it->position() + it->length(), QTextCursor::KeepAnchor);
        } else if (it->previous().isValid()) {
            cursor.setPosition(it->previous().position() + it->previous().length() - 1);
            cursor.setPosition(it->position() + it->length() - 1, QTextCursor::KeepAnchor);
        }
        cursor.removeSelectedText();
    }
}

// Where a bare URL ends, as GitHub's autolinker (cmark-gfm's
// autolink_delim) ends it: trailing punctuation, an entity or ";", and a ")"
// without its "(" in the URL are the prose's.
qsizetype urlEnd(QStringView url)
{
    static const QRegularExpression entity(QStringLiteral("&[A-Za-z0-9]+;$"));
    qsizetype end = url.size();
    while (end > 0) {
        const QChar last = url[end - 1];
        if (QStringView(u"?!.,:*_~'\"").contains(last)) {
            --end;
        } else if (last == u';') {
            const auto found = entity.matchView(url.first(end));
            end = found.hasMatch() ? found.capturedStart() : end - 1;
        } else if (last == u')' && url.first(end).count(u'(') < url.first(end).count(u')')) {
            --end;
        } else {
            break;
        }
    }
    return end;
}

// A bare http(s) URL outside code and links becomes a link, all of it.
void linkUrls(QTextDocument &doc)
{
    static const QRegularExpression url(QStringLiteral("(?<![\\w/])https?://[^\\s<>`]+"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    struct Link {
        int from, to;
        QString href;
    };
    std::vector<Link> links;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        int position = -1;
        QString text;
        const auto scan = [&] {
            for (auto m = url.globalMatch(text); m.hasNext();) {
                const QRegularExpressionMatch found = m.next();
                const QString href = found.captured().first(urlEnd(found.capturedView()));
                const int from = position + int(found.capturedStart());
                if (href.size() > found.captured().indexOf(u"://") + 3)
                    links.push_back({from, from + int(href.size()), href});
            }
            position = -1;
            text.clear();
        };
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (isCode(block, fragment.charFormat()) || fragment.charFormat().isAnchor()) {
                scan();
                continue;
            }
            if (position < 0)
                position = fragment.position();
            text += fragment.text();
        }
        scan();
    }
    for (const Link &link : links) {
        QTextCursor cursor(&doc);
        cursor.setPosition(link.from);
        cursor.setPosition(link.to, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setAnchor(true);
        format.setAnchorHref(link.href);
        cursor.mergeCharFormat(format);
    }
}

} // namespace

WhatsNew::WhatsNew(UpdateChecker &updates, QString cacheFile, QObject *parent)
    : QObject(parent), m_updates(updates), m_cacheFile(std::move(cacheFile))
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
    // The cache's "installed" ran last, its notes from "since": per copy, as
    // copies sharing the settings can run different versions.
    const QString installed = m_updates.version();
    const QJsonObject cache = readCache();
    const QString lastRun = cache.value(QStringLiteral("installed")).toString();
    const QString lastSince = cache.value(QStringLiteral("since")).toString();
    m_since = qs(a::whatsNewSince(lastRun.toStdString(), lastSince.toStdString(), installed.toStdString()));
    m_known = loadCache(cache);
    if (lastRun != installed || lastSince != m_since)
        saveCache(); // the new range, its notes not read yet
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
// notes}]}: the version that ran last, the one its notes start after, and
// the releases between the two, every channel's (none until read).
QJsonObject WhatsNew::readCache() const
{
    if (m_cacheFile.isEmpty())
        return {};
    QFile file(m_cacheFile);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxCacheBytes)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool WhatsNew::loadCache(const QJsonObject &cache)
{
    m_between.clear();
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
    // Each entry's share is itself and its comma. Past the limit, the
    // lowest versions' notes go first, those of versions neither channel
    // shows among its ten before the rest: kept without notes they still
    // count in "And N earlier versions". Then, when that is not enough, the
    // lowest versions. Each pass removes about what is over and measures
    // again.
    QByteArray bytes = content();
    const auto share = [&](qsizetype i) {
        return QJsonDocument(entries.at(i).toObject()).toJson(QJsonDocument::Compact).size() + 1;
    };
    std::vector<bool> shown(releases.size(), false);
    std::size_t stable = 0;
    for (std::size_t i = 0; i < releases.size(); ++i) {
        const bool prerelease = a::isPrerelease(releases[i]);
        shown[i] = i < a::kMaxWhatsNew || (!prerelease && stable < a::kMaxWhatsNew);
        stable += prerelease ? 0 : 1;
    }
    std::vector<qsizetype> order; // whose notes go, in turn
    for (qsizetype i = entries.size() - 1; i >= 0; --i)
        if (!shown[std::size_t(i)])
            order.push_back(i);
    for (qsizetype i = entries.size() - 1; i >= 0; --i)
        if (shown[std::size_t(i)])
            order.push_back(i);
    std::size_t next = 0;
    while (bytes.size() > limit && next < order.size()) {
        for (qint64 over = bytes.size() - limit; over > 0 && next < order.size();) {
            const qsizetype i = order[next++];
            QJsonObject entry = entries.at(i).toObject();
            over -= entry.value(QStringLiteral("notes")).toString().toUtf8().size();
            entry.insert(QStringLiteral("notes"), QString());
            entries.replace(i, entry);
        }
        bytes = content();
    }
    while (bytes.size() > limit && !entries.isEmpty()) {
        for (qint64 over = bytes.size() - limit; over > 0 && !entries.isEmpty();) {
            over -= share(entries.size() - 1);
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
    const QByteArray content =
        m_known ? cacheContent(m_updates.version(), m_since, m_between, kMaxCacheBytes)
                : QJsonDocument(QJsonObject{{QStringLiteral("installed"), m_updates.version()}, {QStringLiteral("since"), m_since}})
                      .toJson(QJsonDocument::Compact);
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
    doc.setMarkdown(markdown, QTextDocument::MarkdownFeatures((int(QTextDocument::MarkdownDialectGitHub) &
                                                               ~(kMd4cPermissiveUrlAutolinks | kMd4cPermissiveWwwAutolinks)) |
                                                              int(QTextDocument::MarkdownNoHTML)));
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
    dropHtml(doc);
    linkUrls(doc);
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
