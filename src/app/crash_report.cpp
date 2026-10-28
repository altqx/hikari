#include "hikari/app/crash_report.h"

#include "hikari/app/analytics.h"
#include "settings_store.h"

#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUuid>
#include <QtVersion>

#include <algorithm>

namespace hikari::app::report {

namespace {

// The extensions a file name is known by: subtitles, video, audio,
// keyframes, scripts, fonts, images, archives and the app's own files.
const QString kExtensions = QStringLiteral(
    "ass|ssa|srt|sub|idx|sup|vtt|sbv|smi|ttml|txt|mkv|mp4|m4v|avi|mov|webm|wmv|m2ts|mts|ts|flv|ogm|ogv|mpg|mpeg|vob|"
    "y4m|avs|vpy|wav|mp3|flac|aac|m4a|ogg|oga|opus|ac3|eac3|dts|mka|wma|w64|lua|moon|ttf|otf|ttc|pfb|woff2?|png|"
    "jpe?g|bmp|gif|webp|zip|7z|rar|json|ini|log|xml|pdf|md|cfg|hikarisub|aegisub|kf|wwxd|xvid|qp");
// An absolute path: C:\ or C:/, \\server\, / or ~/ (not inside a word or a
// fraction such as 1/2).
const QString kPathStart = QStringLiteral(R"((?:(?<![\w.\\/])[A-Za-z]:[\\/]|\\\\[^\\\s]+\\|(?<![\w.:/~\\])~?/)(?=\S))");

// What opens and closes a quotation.
const QList<std::pair<QChar, QChar>> kQuotes{{u'\u201C', u'\u201D'}, {u'"', u'"'},   {u'\u2018', u'\u2019'},
                                              {u'\u00AB', u'\u00BB'}, {u'\u300C', u'\u300D'}, {u'\u300E', u'\u300F'},
                                              {u'\'', u'\''}};

QString fileNameOf(const QString &path)
{
    const qsizetype slash = std::max(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    return path.mid(slash + 1);
}

// `value` as a report may hold it: a word of letters, digits and . _ + -,
// else "custom".
QString plain(const QString &value)
{
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9_.+-]{1,32}$"));
    return ok.match(value).hasMatch() ? value : QStringLiteral("custom");
}

QString onOff(bool on)
{
    return on ? QStringLiteral("on") : QStringLiteral("off");
}

QString severityName(int level)
{
    return level == 2 ? QStringLiteral("Error") : level == 1 ? QStringLiteral("Warning") : QStringLiteral("Info");
}

// Text left-aligned in `width` columns.
QString padded(const QString &text, int width)
{
    return text.leftJustified(width, u' ');
}

} // namespace

QString Scrubber::placeholder(const QString &fileName)
{
    const QString key = fileName.toCaseFolded();
    auto it = m_files.find(key);
    if (it == m_files.end()) {
        const qsizetype dot = fileName.lastIndexOf(u'.');
        const QString ext = dot > 0 ? fileName.mid(dot + 1).toLower() : QString();
        QString name = QStringLiteral("file%1").arg(m_files.size() + 1);
        if (!ext.isEmpty() && plain(ext) == ext && ext.size() <= 10)
            name += u'.' + ext;
        it = m_files.insert(key, name);
    }
    return *it;
}

QString Scrubber::scrub(const QString &input)
{
    QString text = input;
    // A Line's text, wherever it is (longest first, so a Line that holds a
    // shorter one goes whole).
    if (m_texts) {
        QStringList texts = m_texts();
        texts.removeIf([](const QString &t) { return t.trimmed().size() < 4; });
        std::sort(texts.begin(), texts.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const QString &t : std::as_const(texts))
            text.replace(t, QStringLiteral("\u2039text\u203A"));
    }
    // A path with a file name: the name's placeholder; any other path: ‹folder›.
    static const QRegularExpression fileUrl(QStringLiteral("file:/*(?=[A-Za-z]:|/)"), QRegularExpression::CaseInsensitiveOption);
    text.replace(fileUrl, QStringLiteral("/"));
    static const QRegularExpression pathToFile(kPathStart + QStringLiteral(R"([^\n\t"“”«»<>|]*?\.(?:)") + kExtensions +
                                                   QStringLiteral(R"()(?![A-Za-z0-9]))"),
                                               QRegularExpression::CaseInsensitiveOption);
    for (auto m = pathToFile.match(text); m.hasMatch(); m = pathToFile.match(text, m.capturedStart())) {
        const QString name = placeholder(fileNameOf(m.captured()));
        text.replace(m.capturedStart(), m.capturedLength(), name);
    }
    // A folder name may hold spaces: the path goes on past one while the
    // next word holds a separator too (/home/Alice Smith/.local).
    static const QRegularExpression folder(kPathStart + QStringLiteral(R"([^\s"“”«»<>|]+(?:[ \t]+[^\s"“”«»<>|/\\]*[/\\][^\s"“”«»<>|]*)*)"));
    text.replace(folder, QStringLiteral("\u2039folder\u203A"));
    // The names of the files open now, then any other word that is a file name.
    if (m_names) {
        QStringList names = m_names();
        for (QString &n : names)
            n = fileNameOf(n);
        names.removeIf([](const QString &n) { return n.size() < 3; });
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const QString &n : std::as_const(names))
            for (qsizetype at = text.indexOf(n, 0, Qt::CaseInsensitive); at >= 0;
                 at = text.indexOf(n, at, Qt::CaseInsensitive)) {
                const QString name = placeholder(n);
                text.replace(at, n.size(), name);
                at += name.size();
            }
    }
    static const QRegularExpression bareFile(QStringLiteral(R"((?<![\w.])(?!file\d+\.)[^\s"“”«»<>|/\\]+\.(?:)") +
                                                 kExtensions + QStringLiteral(R"()(?![A-Za-z0-9]))"),
                                             QRegularExpression::CaseInsensitiveOption);
    for (auto m = bareFile.match(text); m.hasMatch(); m = bareFile.match(text, m.capturedStart() + 1)) {
        const QString name = placeholder(m.captured());
        text.replace(m.capturedStart(), m.capturedLength(), name);
    }
    // Quoted text: ‹font› after "font", else ‹text›; a quoted placeholder stays.
    static const QRegularExpression placeholderOnly(QStringLiteral("^(file\\d+(\\.[a-z0-9]+)?|\u2039[a-z]+\u203A)$"));
    for (const auto &[open, close] : kQuotes) {
        qsizetype at = 0;
        while ((at = text.indexOf(open, at)) >= 0) {
            // An apostrophe inside a word (don't) opens nothing.
            if (open == u'\'' && at > 0 && text.at(at - 1).isLetterOrNumber()) {
                ++at;
                continue;
            }
            const qsizetype end = text.indexOf(close, at + 1);
            if (end < 0)
                break;
            const QString inner = text.mid(at + 1, end - at - 1);
            if (inner.isEmpty() || placeholderOnly.match(inner).hasMatch()) {
                at = end + 1;
                continue;
            }
            static const QRegularExpression fontBefore(QStringLiteral("\\bfonts?\\s*$"), QRegularExpression::CaseInsensitiveOption);
            const bool font = fontBefore.match(text.left(at)).hasMatch();
            const QString with = font ? QStringLiteral("\u2039font\u203A") : QStringLiteral("\u2039text\u203A");
            text.replace(at + 1, inner.size(), with);
            at += with.size() + 2;
        }
    }
    return text;
}

System System::current(const QString &version, const QString &package, int screens, qreal scale, const QString &layout)
{
    System s;
    s.version = version;
    s.channel = version.contains(u'-') ? QStringLiteral("prerelease") : QStringLiteral("stable");
    s.package = package.isEmpty() ? QStringLiteral("build_tree") : package;
    s.os = Analytics::osId();
    static const QRegularExpression osVersion(QStringLiteral("^[A-Za-z0-9._-]{1,24}$"));
    s.osVersion = QSysInfo::productVersion();
    if (!osVersion.match(s.osVersion).hasMatch())
        s.osVersion = QStringLiteral("other");
    s.arch = Analytics::archId();
    s.qtVersion = QString::fromLatin1(qVersion());
    s.screens = screens;
    s.scale = Analytics::scaleBucket(scale);
    s.layout = layout.isEmpty() ? QStringLiteral("custom") : plain(layout.toLower());
    return s;
}

QList<Setting> settingsSummary(const ui::SettingsStore &settings)
{
    const auto number = [&](const char *id) { return QString::number(settings.integer(id)); };
    const QString language = settings.text("program.language");
    QList<Setting> out{
        {QStringLiteral("theme"), plain(settings.text("appearance.theme"))},
        {QStringLiteral("theme follows system"), onOff(settings.boolean("appearance.followSystem"))},
        {QStringLiteral("title bar"), plain(settings.text("appearance.titleBar"))},
        {QStringLiteral("language"), language.isEmpty() ? QStringLiteral("system") : plain(language)},
        {QStringLiteral("subtitle renderer"),
         settings.text("video.subtitleProvider").isEmpty() ? QStringLiteral("default")
                                                           : plain(settings.text("video.subtitleProvider"))},
        {QStringLiteral("video player"), number("video.playbackPlayer")},
        {QStringLiteral("audio output"), number("audio.outputHostApi")},
        {QStringLiteral("spellchecker"),
         settings.boolean("editor.spellchecker") ? QStringLiteral("on, ") + plain(settings.text("editor.dictionaryLanguage"))
                                                 : QStringLiteral("off")},
        {QStringLiteral("grammar check"), number("grammar.language")},
        {QStringLiteral("autosave"), QStringLiteral("every %1 edits, keep %2")
                                         .arg(number("editor.saveAfterCharacterCount"), number("autosave.maxFiles"))},
        {QStringLiteral("usage data"), onOff(settings.boolean("privacy.usageData"))},
        {QStringLiteral("always send crash reports"), onOff(settings.boolean("privacy.alwaysSendCrashReports"))},
    };
    return out;
}

QString Report::kind() const
{
    if (!record)
        return QStringLiteral("issue");
    return record->freeze() ? QStringLiteral("freeze") : QStringLiteral("crash");
}

QString Report::recordText() const
{
    if (!record)
        return {};
    QStringList lines;
    if (record->freeze())
        lines << QStringLiteral("Freeze") << QStringLiteral("  The window did not respond for %1 s, %2")
                                                 .arg(record->seconds)
                                                 .arg(record->version)
              << QStringLiteral("  Main thread:");
    else
        lines << QStringLiteral("Crash") << QStringLiteral("  %1 (%2) in %3").arg(record->type, record->mechanism, record->version);
    for (qsizetype i = 0; i < record->frames.size(); ++i)
        lines << QStringLiteral("  #%1 %2 +0x%3")
                     .arg(padded(QString::number(i), 2), padded(record->frames[i].module, 18),
                          QString::number(record->frames[i].offset, 16));
    if (record->frames.isEmpty())
        lines << QStringLiteral("  (no frames)");
    return lines.join(u'\n');
}

QString Report::systemText() const
{
    return QStringLiteral("System\n  HikariSub %1 (%2) · %3\n  %4 %5 · %6 · Qt %7 · %8 screen(s) at %9 · layout %10")
        .arg(system.version, system.channel, system.package, system.os, system.osVersion, system.arch, system.qtVersion)
        .arg(system.screens)
        .arg(system.scale, system.layout);
}

QString Report::settingsText() const
{
    QStringList lines{QStringLiteral("Settings")};
    for (const Setting &s : settings)
        lines << QStringLiteral("  ") + padded(s.id, 26) + s.value;
    return lines.join(u'\n');
}

QString Report::logText() const
{
    QStringList lines{QStringLiteral("Log (last %1 lines)").arg(log.size())};
    for (const QString &l : log)
        lines << QStringLiteral("  ") + l;
    if (log.isEmpty())
        lines << QStringLiteral("  (empty)");
    return lines.join(u'\n');
}

QStringList boundLog(QStringList lines)
{
    if (lines.size() > kLogLines)
        lines = lines.mid(lines.size() - kLogLines);
    qsizetype total = 0;
    qsizetype first = lines.size();
    while (first > 0) {
        QString &line = lines[first - 1];
        if (line.size() > kLogLineChars)
            line = line.left(kLogLineChars - 1) + u'…';
        if (total + line.size() + 1 > kLogChars)
            break;
        total += line.size() + 1;
        --first;
    }
    return lines.mid(first);
}

QString Report::text(const QString &note) const
{
    QStringList parts;
    if (const QString n = note.trimmed().left(kNoteChars); !n.isEmpty())
        parts << QStringLiteral("Note\n  ") + QString(n).replace(u'\n', QStringLiteral("\n  "));
    if (record)
        parts << recordText();
    parts << systemText() << settingsText() << logText();
    return parts.join(QStringLiteral("\n\n"));
}

QJsonObject Report::exceptionProperties() const
{
    if (!record)
        return {};
    QJsonArray frames;
    for (const crash::Frame &frame : record->frames)
        frames.append(QJsonObject{{QStringLiteral("platform"), QStringLiteral("native")},
                                  {QStringLiteral("module"), frame.module},
                                  {QStringLiteral("offset"), QStringLiteral("0x") + QString::number(frame.offset, 16)}});
    const QJsonObject exception{
        {QStringLiteral("type"), record->type},
        {QStringLiteral("value"), record->type},
        {QStringLiteral("mechanism"),
         QJsonObject{{QStringLiteral("type"), record->mechanism}, {QStringLiteral("handled"), false}}},
        {QStringLiteral("stacktrace"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("raw")}, {QStringLiteral("frames"), frames}}}};
    QJsonObject out{{QStringLiteral("$exception_list"), QJsonArray{exception}},
                    {QStringLiteral("$exception_level"), recovered ? QStringLiteral("error") : QStringLiteral("fatal")},
                    {QStringLiteral("crashed_version"), record->version},
                    {QStringLiteral("report_id"), id}};
    if (record->freeze())
        out.insert(QStringLiteral("freeze_seconds"), record->seconds);
    return out;
}

QJsonObject Report::reportProperties(const QString &note) const
{
    QJsonObject settingsJson;
    for (const Setting &s : settings)
        settingsJson.insert(s.id, s.value);
    QJsonObject out{{QStringLiteral("report_id"), id},
                    {QStringLiteral("kind"), kind()},
                    {QStringLiteral("version"), system.version},
                    {QStringLiteral("channel"), system.channel},
                    {QStringLiteral("$os"), system.os},
                    {QStringLiteral("$os_version"), system.osVersion},
                    {QStringLiteral("arch"), system.arch},
                    {QStringLiteral("package"), system.package},
                    {QStringLiteral("qt_version"), system.qtVersion},
                    {QStringLiteral("settings"), settingsJson},
                    {QStringLiteral("log"), log.join(u'\n')}};
    if (const QString n = note.trimmed().left(kNoteChars); !n.isEmpty())
        out.insert(QStringLiteral("note"), n);
    return out;
}

QJsonObject Report::toJson() const
{
    QJsonObject out{{QStringLiteral("id"), id},
                    {QStringLiteral("when"), when.toUTC().toString(Qt::ISODate)},
                    {QStringLiteral("exceptionSent"), exceptionSent},
                    {QStringLiteral("recovered"), recovered},
                    {QStringLiteral("log"), QJsonArray::fromStringList(log)}};
    if (record) {
        QJsonArray frames;
        for (const crash::Frame &f : record->frames)
            frames.append(QJsonArray{f.module, QStringLiteral("0x") + QString::number(f.offset, 16)});
        out.insert(QStringLiteral("record"), QJsonObject{{QStringLiteral("type"), record->type},
                                                         {QStringLiteral("mechanism"), record->mechanism},
                                                         {QStringLiteral("version"), record->version},
                                                         {QStringLiteral("seconds"), record->seconds},
                                                         {QStringLiteral("frames"), frames}});
    }
    return out;
}

std::optional<Report> Report::fromJson(const QJsonObject &json)
{
    Report r;
    r.id = json.value(QStringLiteral("id")).toString();
    if (QUuid::fromString(r.id).isNull())
        return std::nullopt;
    r.when = QDateTime::fromString(json.value(QStringLiteral("when")).toString(), Qt::ISODate);
    r.exceptionSent = json.value(QStringLiteral("exceptionSent")).toBool();
    r.recovered = json.value(QStringLiteral("recovered")).toBool();
    for (const QJsonValue &l : json.value(QStringLiteral("log")).toArray())
        r.log << l.toString();
    r.log = boundLog(r.log);
    const QJsonObject rec = json.value(QStringLiteral("record")).toObject();
    if (!rec.isEmpty()) {
        crash::Record record{rec.value(QStringLiteral("type")).toString(), rec.value(QStringLiteral("mechanism")).toString(),
                             rec.value(QStringLiteral("version")).toString(), {}, rec.value(QStringLiteral("seconds")).toInt()};
        static const QRegularExpression module(QStringLiteral("^[A-Za-z0-9_.+-]{1,80}$"));
        for (const QJsonValue &f : rec.value(QStringLiteral("frames")).toArray()) {
            const QJsonArray pair = f.toArray();
            const QString offset = pair.at(1).toString();
            if (!module.match(pair.at(0).toString()).hasMatch() || !offset.startsWith(QLatin1String("0x")))
                return std::nullopt;
            record.frames.append({pair.at(0).toString(), offset.mid(2).toULongLong(nullptr, 16)});
        }
        if (record.frames.size() > crash::kMaxFrames)
            return std::nullopt;
        r.record = record;
    }
    return r;
}

QString Issue::templateFile(Kind kind)
{
    switch (kind) {
    case Feature: return QStringLiteral("feature.yml");
    case Question: return QStringLiteral("question.yml");
    case Bug: break;
    }
    return QStringLiteral("bug.yml");
}

QUrl Issue::newIssueBase()
{
    return QUrl(QStringLiteral("https://github.com/altqx/hikari/issues/new"));
}

QString Issue::markdown() const
{
    static const QHash<QString, QString> headings{
        {QStringLiteral("what-happened"), QStringLiteral("What happened")},
        {QStringLiteral("steps"), QStringLiteral("Steps to reproduce")},
        {QStringLiteral("expected"), QStringLiteral("What you expected")},
        {QStringLiteral("crash-report"), QStringLiteral("Crash or freeze report")},
        {QStringLiteral("wanted"), QStringLiteral("What you'd like")},
        {QStringLiteral("why"), QStringLiteral("What it would help with")},
        {QStringLiteral("question"), QStringLiteral("Your question")},
        {QStringLiteral("system"), QStringLiteral("System and settings")},
        {QStringLiteral("log"), QStringLiteral("Log excerpt")},
    };
    QStringList parts;
    if (!title.isEmpty())
        parts << QStringLiteral("## ") + title;
    for (const auto &[id, value] : fields) {
        if (value.trimmed().isEmpty())
            continue;
        const bool code = id == QLatin1String("crash-report") || id == QLatin1String("system") || id == QLatin1String("log");
        parts << QStringLiteral("### ") + headings.value(id, id) + u'\n' +
                     (code ? QStringLiteral("```\n") + value + QStringLiteral("\n```") : value);
    }
    return parts.join(QStringLiteral("\n\n"));
}

Issue::Link Issue::link(int maxLength) const
{
    const auto item = [](const QString &key, const QString &value) {
        return key.toLatin1() + '=' + QUrl::toPercentEncoding(value);
    };
    const QByteArray base = newIssueBase().toEncoded() + '?' + item(QStringLiteral("template"), templateFile(kind)) + '&' +
                            item(QStringLiteral("title"), title);
    QByteArray full = base;
    for (const auto &[id, value] : fields)
        if (!value.trimmed().isEmpty())
            full += '&' + item(id, value);
    if (full.size() > maxLength)
        return {QUrl::fromEncoded(base, QUrl::StrictMode), false};
    return {QUrl::fromEncoded(full, QUrl::StrictMode), true};
}

Issue Issue::fromReport(const Report &report, const QString &note)
{
    Issue issue;
    issue.kind = Bug;
    if (report.record)
        issue.title = report.record->freeze()
                          ? QStringLiteral("Freeze: the window did not respond for %1 s in %2")
                                .arg(report.record->seconds)
                                .arg(report.system.version)
                          : QStringLiteral("Crash: %1 in %2").arg(report.record->type, report.system.version);
    issue.fields = {{QStringLiteral("what-happened"), note.trimmed()},
                    {QStringLiteral("crash-report"), report.recordText()},
                    {QStringLiteral("system"), report.systemText() + QStringLiteral("\n\n") + report.settingsText()},
                    {QStringLiteral("log"), report.log.join(u'\n')}};
    return issue;
}

} // namespace hikari::app::report
