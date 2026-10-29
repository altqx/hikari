#include "hikari/app/whats_new.h"

#include "hikari/app/package_kind.h"
#include "hikari/backends/desktop_handoff.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

QVariantMap shownRelease(const a::ReleaseEntry &release)
{
    const QString tag = qs(release.tag);
    const QString version = tag.mid(1);
    const QString name = qs(release.name);
    const QDateTime published = QDateTime::fromString(qs(release.published), Qt::ISODate);
    return {{QStringLiteral("version"), version},
            {QStringLiteral("name"), name.isEmpty() || name == tag ? QStringLiteral("HikariSub ") + version : name},
            {QStringLiteral("date"), published.isValid() ? published.toLocalTime().date() : QDate()},
            {QStringLiteral("prerelease"), a::isPrerelease(release)},
            {QStringLiteral("notes"), qs(release.notes)}};
}

// md4c flags (Qt passes MarkdownFeatures through): its permissive autolinks stop at "...", which
// GitHub's ".../compare/v0.1.2...v0.1.3" lines hold, so linkUrls() links bare URLs instead.
constexpr int kMd4cPermissiveUrlAutolinks = 0x0004;
constexpr int kMd4cPermissiveWwwAutolinks = 0x0400;

bool isCode(const QTextBlock &block, const QTextCharFormat &format)
{
    return block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage) ||
           block.blockFormat().hasProperty(QTextFormat::BlockCodeFence) || format.fontFixedPitch();
}

// md4c misreads some spellings, so they are swapped for private-use markers before parsing and
// put back after: escaped brackets ("\\<", "&lt;") come out raw, Qt's import drops a linked
// image's link, and a raw tag over a hard line break splits into two blocks.
class SourceMarks
{
public:
    QString mark(const QString &markdown)
    {
        m_byMarker.assign(kLast - kFirst + 1, -1);
        for (const QChar c : markdown)
            if (c.unicode() >= kFirst && c.unicode() <= kLast)
                m_byMarker[c.unicode() - kFirst] = kHeld;
        // A raw tag over lines as CommonMark reads one: no blank line, no line opening a quote.
        static const QRegularExpression tagOverLines(QStringLiteral(
            "<(?:(?!(?i:script|style)(?![A-Za-z0-9-]))[A-Za-z][A-Za-z0-9-]*"
            "(?>\\s+[A-Za-z_:][A-Za-z0-9_.:-]*(?>\\s*=\\s*(?>[^\\s\"'=<>`]+|'[^']*'|\"[^\"]*\"))?)*+\\s*/?"
            "|/[A-Za-z][A-Za-z0-9-]*\\s*)>"));
        static const QRegularExpression notInTag(QStringLiteral("\\n[ \\t]*(?:\\n|[ ]{0,3}>)"));
        QString tagged;
        qsizetype at = 0;
        for (auto m = tagOverLines.globalMatch(markdown); m.hasNext();) {
            const QRegularExpressionMatch found = m.next();
            const QString written = found.captured();
            // After an odd run of backslashes the "<" is escaped: text.
            qsizetype slashes = 0;
            while (found.capturedStart() - slashes > 0 && markdown[found.capturedStart() - slashes - 1] == u'\\')
                ++slashes;
            if (slashes % 2 || !written.contains(u'\n') || notInTag.match(written).hasMatch())
                continue;
            const QChar marker = markerFor(written, QString(), QString(), true);
            if (marker.isNull())
                continue;
            tagged += QStringView(markdown).sliced(at, found.capturedStart() - at);
            tagged += marker;
            at = found.capturedEnd();
        }
        tagged += QStringView(markdown).sliced(at);
        static const QRegularExpression linkedImage = [] {
            const QString bare = QStringLiteral("[^\\s()<>`\\\\]|\\\\[^\\s`]");
            const QString destination = QStringLiteral("\\s*(?:<[^<>`\\n]*>|(?:") + bare + QStringLiteral("|(\\((?:") +
                                        bare + QStringLiteral("|(?-1))*\\)))*)") +
                                        QStringLiteral("(?:\\s+(?:\"[^\"`\\n]*\"|'[^'`\\n]*'))?\\s*");
            const QString alt = QStringLiteral("(?<alt>(?:[^\\[\\]\\\\`\\n]|\\\\[^`\\n])*)");
            const QString label = QStringLiteral("\\[(?:[^\\[\\]\\\\`\\n]|\\\\[^`\\n])+\\]");
            return QRegularExpression(QStringLiteral("\\[!\\[") + alt + QStringLiteral("\\](?:\\(") + destination +
                                      QStringLiteral("\\)|") + label + QStringLiteral("|\\[\\])\\](?<outer>\\(") +
                                      destination + QStringLiteral("\\)|") + label + QStringLiteral(")"));
        }();
        QString linked;
        at = 0;
        for (auto m = linkedImage.globalMatch(tagged); m.hasNext();) {
            const QRegularExpressionMatch found = m.next();
            const QString shown =
                QLatin1Char('[') + found.captured(u"alt") + QLatin1Char(']') + found.captured(u"outer");
            const QChar marker = markerFor(found.captured(), shown, QString());
            linked += QStringView(tagged).sliced(at, found.capturedStart() - at);
            linked += marker.isNull() ? found.captured() : marker + shown;
            at = found.capturedEnd();
        }
        linked += QStringView(tagged).sliced(at);
        // A backslash escapes any ASCII punctuation, so all are matched and only brackets marked.
        static const QRegularExpression escaped(
            QStringLiteral("\\\\[!-/:-@\\[-`{-~]|&(?:lt|LT|gt|GT|#0*6[02]|#[xX]0*3[cCeE]);"));
        static const QRegularExpression less(QStringLiteral("^(?:\\\\<|&(?:lt|LT|#0*60|#[xX]0*3[cC]);)$"));
        QString out;
        out.reserve(linked.size());
        at = 0;
        for (auto m = escaped.globalMatch(linked); m.hasNext();) {
            const QRegularExpressionMatch found = m.next();
            const QString written = found.captured();
            if (written.size() == 2 && written[1] != u'<' && written[1] != u'>')
                continue;
            const QChar marker =
                markerFor(written, QString(), less.match(written).hasMatch() ? QStringLiteral("<") : QStringLiteral(">"));
            if (marker.isNull())
                continue;
            out += QStringView(linked).sliced(at, found.capturedStart() - at);
            out += marker;
            at = found.capturedEnd();
        }
        out += QStringView(linked).sliced(at);
        return out;
    }

    void restoreTags(QTextDocument &doc) const
    {
        forEachFragment(doc, [&](const QTextBlock &block, const QString &text, const QTextFragment &fragment,
                                 std::vector<Change> &changes) {
            const bool codeBlock = block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage) ||
                                   block.blockFormat().hasProperty(QTextFormat::BlockCodeFence);
            // QTextFragment::length() walks the fragment's pieces.
            const int from = fragment.position() - block.position(), to = from + fragment.length();
            for (int i = from; i < to; ++i)
                if (const Mark *mark = find(text[i]); mark && mark->tag)
                    changes.push_back({block.position() + i, 1,
                                       codeBlock ? mark->written : QString(mark->written).replace(u'\n', u' '),
                                       fragment.charFormat()});
        });
    }

    void restore(QTextDocument &doc) const
    {
        if (m_marks.empty())
            return;
        forEachFragment(doc, [&](const QTextBlock &, const QString &, const QTextFragment &fragment,
                                 std::vector<Change> &changes) {
            QTextCharFormat format = fragment.charFormat();
            if (!format.isAnchor())
                return;
            const QString written = format.anchorHref();
            QString href;
            bool marked = false;
            for (const QChar c : written) {
                const Mark *mark = find(c);
                href += mark ? mark->meant : QString(c);
                marked = marked || mark;
            }
            if (!marked)
                return;
            format.setAnchorHref(href);
            changes.push_back({fragment.position(), fragment.length(), fragment.text(), format});
        });
        for (const bool linked : {false, true}) {
            forEachFragment(doc, [&](const QTextBlock &block, const QString &text, const QTextFragment &fragment,
                                     std::vector<Change> &changes) {
                const bool code = isCode(block, fragment.charFormat());
                const int from = fragment.position() - block.position(), to = from + fragment.length();
                for (int i = from; i < to; ++i) {
                    const Mark *mark = find(text[i]);
                    if (!mark || mark->tag || mark->shown.isEmpty() == linked)
                        continue;
                    const bool asRead = linked && code && QStringView(text).sliced(i + 1).startsWith(mark->shown);
                    const QString with = linked ? (asRead ? mark->written : QString())
                                                : (code ? mark->written : mark->meant);
                    changes.push_back({block.position() + i, asRead ? 1 + int(mark->shown.size()) : 1, with,
                                       fragment.charFormat()});
                }
            });
        }
    }

private:
    struct Change {
        int position, length;
        QString text;
        QTextCharFormat format;
    };
    template<typename Gather>
    static void forEachFragment(QTextDocument &doc, Gather gather)
    {
        std::vector<Change> changes;
        for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
            const QString text = block.text();
            for (auto it = block.begin(); !it.atEnd(); ++it)
                gather(block, text, it.fragment(), changes);
        }
        QTextCursor cursor(&doc);
        for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
            cursor.setPosition(it->position);
            cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
            cursor.insertText(it->text, it->format);
        }
    }
    static constexpr char16_t kFirst = 0xE000, kLast = 0xF8FF;
    static constexpr int kHeld = -2;
    struct Mark {
        QChar marker;
        QString written;
        QString shown;
        QString meant;
        bool tag = false;
    };
    QChar markerFor(const QString &written, const QString &shown, const QString &meant, bool tag = false)
    {
        if (const auto known = m_byWritten.constFind(written); known != m_byWritten.cend())
            return m_marks[*known].marker;
        while (m_next <= kLast && m_byMarker[m_next - kFirst] != -1)
            ++m_next;
        if (m_next > kLast)
            return {};
        m_byMarker[m_next - kFirst] = int(m_marks.size());
        m_byWritten.insert(written, int(m_marks.size()));
        m_marks.push_back({QChar(m_next++), written, shown, meant, tag});
        return m_marks.back().marker;
    }
    const Mark *find(QChar c) const
    {
        if (c.unicode() < kFirst || c.unicode() > kLast || m_byMarker[c.unicode() - kFirst] < 0)
            return nullptr;
        return &m_marks[m_byMarker[c.unicode() - kFirst]];
    }
    std::vector<Mark> m_marks;
    std::vector<int> m_byMarker;
    QHash<QString, int> m_byWritten;
    char16_t m_next = kFirst;
};

// MarkdownNoHTML would keep raw HTML as text, so it is dropped here, outside code.
void dropHtml(QTextDocument &doc)
{
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][A-Za-z0-9-]*(\\s(?:[^>\"']|\"[^\"]*\"|'[^']*')*)?/?>"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression dropped(
        QStringLiteral("<!--|<\\?|<!\\[CDATA\\[|<![A-Za-z]|<(script|style)(?=[\\s>]|$)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
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
    QRegularExpression closer;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
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
            qsizetype at = 0;
            while (at < run.text.size()) {
                // md4c reads backticks in raw HTML as code: a drop runs through it to its closer.
                if (!closer.pattern().isEmpty()) {
                    const QRegularExpressionMatch end = closer.match(run.text, at);
                    const qsizetype to = end.hasMatch() ? end.capturedEnd() : run.text.size();
                    drop(run.position + int(at), run.position + int(to));
                    if (end.hasMatch())
                        closer = QRegularExpression();
                    at = to;
                    continue;
                }
                if (run.code)
                    break;
                const QRegularExpressionMatch open = dropped.match(run.text, at);
                const QRegularExpressionMatch element = tag.match(run.text, at);
                if (open.hasMatch() && (!element.hasMatch() || open.capturedStart() <= element.capturedStart())) {
                    drop(run.position + int(open.capturedStart()), run.position + int(open.capturedEnd()));
                    const QString opened = open.captured();
                    closer = !open.captured(1).isEmpty()
                                 ? QRegularExpression(QStringLiteral("</%1\\s*>").arg(open.captured(1)),
                                                      QRegularExpression::CaseInsensitiveOption |
                                                          QRegularExpression::UseUnicodePropertiesOption)
                             : opened == u"<!--"         ? QRegularExpression(QStringLiteral("-->"))
                             : opened == u"<?"           ? QRegularExpression(QStringLiteral("\\?>"))
                             : opened == u"<![CDATA["    ? QRegularExpression(QStringLiteral("\\]\\]>"))
                                                         : QRegularExpression(QStringLiteral(">"));
                    at = open.capturedEnd();
                } else if (element.hasMatch()) {
                    drop(run.position + int(element.capturedStart()), run.position + int(element.capturedEnd()));
                    at = element.capturedEnd();
                } else {
                    break;
                }
            }
        }
        if (!closer.pattern().isEmpty() && block.next().isValid())
            drop(block.position() + block.length() - 1, block.position() + block.length());
        if (int(gone.size()) != before || (!gone.empty() && gone.back().to > block.position()))
            block.setUserState(1);
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

// Where cmark-gfm's autolink_delim ends a bare URL: trailing punctuation, an entity or ";", and
// a ")" without its "(" are the prose's.
qsizetype urlEnd(QStringView url)
{
    qsizetype opening = url.count(u'(');
    qsizetype closing = url.count(u')');
    qsizetype end = url.size();
    while (end > 0) {
        const QChar last = url[end - 1];
        if (QStringView(u"?!.,:*_~'\"").contains(last)) {
            --end;
        } else if (last == u';') {
            qsizetype from = end - 1;
            while (from > 0 && url[from - 1].isLetterOrNumber() && url[from - 1].unicode() < 0x80)
                --from;
            end = from > 0 && from < end - 1 && url[from - 1] == u'&' ? from - 1 : end - 1;
        } else if (last == u')' && opening < closing) {
            --closing;
            --end;
        } else {
            break;
        }
    }
    return end;
}

void linkUrls(QTextDocument &doc)
{
    static const QRegularExpression url(QStringLiteral("(?<![\\w/])https?://[^\\s<>`\\x{E000}-\\x{F8FF}]+"),
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

}

WhatsNew::WhatsNew(UpdateChecker &updates, QString cacheFile, QObject *parent)
    : QObject(parent), m_updates(updates), m_cacheFile(std::move(cacheFile))
{
    connect(&m_updates, &UpdateChecker::releasesRead, this, &WhatsNew::listRead);
    connect(&m_updates, &UpdateChecker::optionsChanged, this, [this] {
        if (m_started)
            fetchIfNeeded();
        show();
        emit changed();
    });
}

void WhatsNew::start()
{
    m_started = true;
    const QString installed = m_updates.version();
    const QJsonObject cache = readCache();
    const QString lastRun = cache.value(QStringLiteral("installed")).toString();
    const QString lastSince = cache.value(QStringLiteral("since")).toString();
    m_since = qs(a::whatsNewSince(lastRun.toStdString(), lastSince.toStdString(), installed.toStdString()));
    m_known = loadCache(cache);
    if (lastRun != installed || lastSince != m_since)
        saveCache();
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
    m_updates.readReleases();
}

void WhatsNew::listRead(bool ok)
{
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
    std::vector<qsizetype> order;
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
    SourceMarks marks;
    doc.setMarkdown(marks.mark(markdown), QTextDocument::MarkdownFeatures((int(QTextDocument::MarkdownDialectGitHub) &
                                                               ~(kMd4cPermissiveUrlAutolinks | kMd4cPermissiveWwwAutolinks)) |
                                                              int(QTextDocument::MarkdownNoHTML)));
    // One edit block, or QTextDocument lays out again at each edit (seconds on a long block).
    QTextCursor edits(&doc);
    edits.beginEditBlock();
    marks.restoreTags(doc);
    dropHtml(doc);
    linkUrls(doc);
    // MUST KILL: must run after dropHtml, or an alt text's escaped "<b>" is dropped as HTML.
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
    marks.restore(doc);
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
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
    edits.endEditBlock();
    // toHtml() puts the document's font on <body>; the view's font should apply.
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
    QString page = feed.pageBase;
    if (page.endsWith(QLatin1String("/tag/")))
        page.chop(5);
    return QUrl(page);
}

QString WhatsNew::cacheFileIn(const UpdateEnvironment &environment)
{
    if (environment.cacheDir.isEmpty())
        return {};
    const QString &file = environment.package.kind == a::PackageKind::AppImage ? environment.package.appImage
                                                                                : environment.executable;
    const auto key = a::whatsNewCopyKey(packageKindToken(environment.package.kind).toStdString(), file.toStdString());
    return environment.cacheDir + QStringLiteral("/whats-new-%1.json").arg(qs(key));
}

}
