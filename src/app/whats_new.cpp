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
//   style's goes with it, code and fences inside it too;
// - a bare URL becomes an autolink ("<url>"), so all of it is the link:
//   md4c's own detection stops at "...", and GitHub's "Full Changelog"
//   lines read ".../compare/v0.1.2...v0.1.3". Not in a link's own syntax;
//   trailing punctuation stays outside, as on GitHub.
// Code as CommonMark has it: a fence closes on a line of its own character
// at least as long as the opening one; a block indented four columns after
// a blank line is code (eight under a list item, whose text is indented
// four); a span runs from a run of backticks to the next run of as many,
// over line breaks but not a blank line, and a run with none is plain
// backticks.
QString prepareMarkdown(const QString &markdown)
{
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][A-Za-z0-9-]*(\\s[^>]*)?/?>"));
    static const QRegularExpression url(
        QStringLiteral("(^|[^(<\\[\\w/\"'=])(https?://[^\\s<>`]*[^\\s<>`.,:;!?'\")\\]*_~])"));
    // What is dropped whole, up to its end, over lines too.
    static const QRegularExpression dropped(QStringLiteral("<!--|<(script|style)(?=[\\s>/])"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    static const QRegularExpression blankLine(QStringLiteral("\n[ \t]*\n"));
    static const QRegularExpression listItem(QStringLiteral("^( {0,3}(?:[-*+]|\\d{1,9}[.)]))( {1,4}|$)"));
    const auto text = [](QString part) { return part.remove(tag).replace(url, QStringLiteral("\\1<\\2>")); };
    const QString &md = markdown;
    const auto lineEnd = [&](qsizetype from) {
        const qsizetype end = md.indexOf(QLatin1Char('\n'), from);
        return end < 0 ? md.size() : end;
    };
    // Columns of leading space (a tab to the next multiple of four).
    const auto indentOf = [](const QString &line) {
        qsizetype columns = 0;
        for (const QChar c : line) {
            if (c == QLatin1Char(' '))
                ++columns;
            else if (c == QLatin1Char('\t'))
                columns += 4 - columns % 4;
            else
                break;
        }
        return columns;
    };
    const auto blank = [](const QString &line) { return line.trimmed().isEmpty(); };
    const auto runAt = [&](qsizetype from, QChar c) {
        qsizetype end = from;
        while (end < md.size() && md.at(end) == c)
            ++end;
        return end - from;
    };
    QString kept;
    QRegularExpression closer; // empty while nothing is being dropped
    bool lineStart = true;
    bool afterBlank = true; // the line before was blank (or there was none)
    qsizetype listText = -1; // the last list item's text column; -1: no list
    qsizetype at = 0;
    while (at < md.size()) {
        if (!closer.pattern().isEmpty()) {
            const QRegularExpressionMatch end = closer.match(md, at);
            if (!end.hasMatch())
                break;
            closer = QRegularExpression();
            at = end.capturedEnd();
            lineStart = false;
            continue;
        }
        if (lineStart) {
            const qsizetype end = lineEnd(at);
            const QString line = md.mid(at, end - at);
            const bool wasAfterBlank = afterBlank;
            afterBlank = blank(line);
            const qsizetype indent = indentOf(line);
            const qsizetype codeIndent = (listText >= 0 ? listText : 0) + 4;
            if (!afterBlank && wasAfterBlank && indent >= codeIndent) {
                // An indented code block: verbatim while its lines are blank
                // or as indented.
                qsizetype stop = end;
                while (stop < md.size()) {
                    const qsizetype next = lineEnd(stop + 1);
                    const QString following = md.mid(stop + 1, next - stop - 1);
                    if (!blank(following) && indentOf(following) < codeIndent)
                        break;
                    stop = next;
                }
                kept += md.mid(at, stop - at);
                at = stop; // at the line break: an empty line, no paragraph
                continue;
            }
            if (!afterBlank) {
                const QRegularExpressionMatch item = listItem.match(line);
                if (item.hasMatch())
                    listText = item.capturedLength(1) + std::max<qsizetype>(item.capturedLength(2), 1);
                else if (wasAfterBlank && listText >= 0 && indent < listText)
                    listText = -1; // a paragraph after the list
            }
            const QRegularExpressionMatch open = fence.match(line);
            if (open.hasMatch()) {
                // Verbatim to the closing fence's line (or the end).
                const QChar c = open.captured(1).at(0);
                const qsizetype length = open.capturedLength(1);
                qsizetype line = end;
                while (line < md.size()) {
                    const qsizetype next = line + 1;
                    const qsizetype stop = lineEnd(next);
                    qsizetype indent = next;
                    while (indent < stop && indent - next < 4 && md.at(indent) == QLatin1Char(' '))
                        ++indent;
                    const qsizetype run = indent - next < 4 ? runAt(indent, c) : 0;
                    line = stop;
                    if (run >= length && md.mid(indent + run, stop - indent - run).trimmed().isEmpty())
                        break;
                }
                kept += md.mid(at, line - at);
                at = line;
                continue; // at the line break, or the end
            }
        }
        // Text to the next backtick or line break.
        qsizetype stop = at;
        while (stop < md.size() && md.at(stop) != QLatin1Char('`') && md.at(stop) != QLatin1Char('\n'))
            ++stop;
        const QString part = md.mid(at, stop - at);
        // The line's end can end a tag's name (<script, then its attributes).
        const bool lineEnds = stop >= md.size() || md.at(stop) == QLatin1Char('\n');
        const QRegularExpressionMatch open = dropped.match(lineEnds ? part + QLatin1Char('\n') : part);
        if (open.hasMatch()) {
            kept += text(part.left(open.capturedStart()));
            closer = open.captured(1).isEmpty()
                         ? QRegularExpression(QStringLiteral("-->"))
                         : QRegularExpression(QStringLiteral("</%1\\s*>").arg(open.captured(1)),
                                              QRegularExpression::CaseInsensitiveOption);
            at += open.capturedEnd();
            lineStart = false;
            continue;
        }
        kept += text(part);
        at = stop;
        if (at >= md.size())
            break;
        if (md.at(at) == QLatin1Char('\n')) {
            kept += QLatin1Char('\n');
            ++at;
            lineStart = true;
            continue;
        }
        // A code span: to the next run of as many backticks, within the
        // paragraph; with none, the run is plain backticks.
        const qsizetype run = runAt(at, QLatin1Char('`'));
        const QRegularExpressionMatch paragraph = blankLine.match(md, at);
        const qsizetype limit = paragraph.hasMatch() ? paragraph.capturedStart() : md.size();
        qsizetype close = -1;
        for (qsizetype next = md.indexOf(QLatin1Char('`'), at + run); next >= 0 && next < limit;) {
            const qsizetype length = runAt(next, QLatin1Char('`'));
            if (length == run) {
                close = next;
                break;
            }
            next = md.indexOf(QLatin1Char('`'), next + length);
        }
        const qsizetype end = close < 0 ? at + run : close + run;
        kept += md.mid(at, end - at);
        at = end;
        lineStart = false;
    }
    return kept;
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
