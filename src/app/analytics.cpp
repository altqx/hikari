#include "hikari/app/analytics.h"

#include "hikari/app/crash_record.h"
#include "hikari/app/localisation.h"
#include "hikari/application/hotkeys.h"
#include "theme.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QUuid>
#include <QtVersion>

#include <initializer_list>
#include <map>

namespace hikari::app {

namespace {

constexpr char kEnabled[] = "privacy.usageData";
constexpr char kId[] = "privacy.usageDataId";

struct Rule {
    enum Kind { OneOf, Pattern, Bool, Count, ListOf, Exceptions, Text, Settings } kind;
    QStringList values;
    QString pattern;
    // MUST KILL: max is Text's length cap but Settings' entry-count cap.
    int max = 0;
};

Rule oneOf(QStringList values)
{
    return {Rule::OneOf, std::move(values), {}};
}

Rule pattern(QString p)
{
    return {Rule::Pattern, {}, std::move(p)};
}

const QStringList kTimeBuckets{QStringLiteral("<250ms"), QStringLiteral("250-500ms"), QStringLiteral("0.5-1s"),
                               QStringLiteral("1-2s"),   QStringLiteral("2-5s"),      QStringLiteral("5-15s"),
                               QStringLiteral("15-60s"), QStringLiteral("60s+")};
const QStringList kSessionBuckets{QStringLiteral("<1m"),    QStringLiteral("1-5m"), QStringLiteral("5-15m"),
                                  QStringLiteral("15-60m"), QStringLiteral("1-3h"), QStringLiteral("3h+")};
const QStringList kLayouts{QStringLiteral("editing"),     QStringLiteral("timing"), QStringLiteral("translation"),
                           QStringLiteral("typesetting"), QStringLiteral("karaoke"), QStringLiteral("saved")};
const std::map<QString, QString> kPanels{
    {QStringLiteral("Video"), QStringLiteral("video")},
    {QStringLiteral("Audio"), QStringLiteral("audio")},
    {QStringLiteral("Editor"), QStringLiteral("editor")},
    {QStringLiteral("Grid"), QStringLiteral("grid")},
    {QStringLiteral("Reference"), QStringLiteral("reference")},
    {QStringLiteral("Timing"), QStringLiteral("timing")},
    {QStringLiteral("Search"), QStringLiteral("search")},
    {QStringLiteral("Automation"), QStringLiteral("automation")},
    {QStringLiteral("Original"), QStringLiteral("original")},
    {QStringLiteral("Log"), QStringLiteral("log")},
    {QStringLiteral("Styles"), QStringLiteral("styles")},
    {QStringLiteral("FixMinorErrors"), QStringLiteral("fix_minor_errors")},
    {QStringLiteral("Template"), QStringLiteral("template")},
    {QStringLiteral("Spellchecker"), QStringLiteral("spellchecker")},
};
const QStringList kDialogs{
    QStringLiteral("about"),          QStringLiteral("allTagsEdition"), QStringLiteral("aspectRatio"),
    QStringLiteral("autosaves"),      QStringLiteral("columns"),        QStringLiteral("conversion"),
    QStringLiteral("crashReport"),    QStringLiteral("credits"),        QStringLiteral("dummyVideo"),
    QStringLiteral("fontCatalogClashes"),
    QStringLiteral("fontCatalogs"),   QStringLiteral("fontCollector"),  QStringLiteral("fontPicker"),
    QStringLiteral("fontsFromSubtitles"), QStringLiteral("gettingStarted"), QStringLiteral("history"),
    QStringLiteral("hotkeyCapture"),  QStringLiteral("kanjiTimer"),     QStringLiteral("karaokeCheck"),
    QStringLiteral("listChooser"),    QStringLiteral("manageLayouts"),  QStringLiteral("movePanel"),
    QStringLiteral("reportIssue"),    QStringLiteral("resample"),       QStringLiteral("saveLayout"),     QStringLiteral("scriptProperties"),
    QStringLiteral("selectLines"),    QStringLiteral("setNewFps"),      QStringLiteral("settings"),
    QStringLiteral("settingsImport"), QStringLiteral("shapesEdition"),  QStringLiteral("spellAddedWords"),
    QStringLiteral("styleClashes"),   QStringLiteral("styleManager"),   QStringLiteral("translationShift"),
    QStringLiteral("unsavedChanges"), QStringLiteral("updateAvailable"), QStringLiteral("whatsNew"),
    QStringLiteral("automation_script")};
const QStringList kLanguages{QStringLiteral("en"), QStringLiteral("ko_KR"), QStringLiteral("pl"), QStringLiteral("ta"),
                             QStringLiteral("th_TH")};
const QStringList kPages{QStringLiteral("language"), QStringLiteral("import"), QStringLiteral("look"),
                         QStringLiteral("layout"),   QStringLiteral("media"),  QStringLiteral("editing"),
                         QStringLiteral("updates"),  QStringLiteral("files")};
const QString kUuid = QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
const QString kVersion = QStringLiteral("^[0-9]{1,4}\\.[0-9]{1,4}\\.[0-9]{1,4}([-+][0-9A-Za-z.]{1,24})?$");

using Table = std::map<QString, std::map<QString, Rule>>;

const Table &table()
{
    static const Table events = [] {
        Table t;
        std::map<QString, Rule> started{
            {QStringLiteral("version"), pattern(kVersion)},
            {QStringLiteral("channel"), oneOf({QStringLiteral("stable"), QStringLiteral("prerelease")})},
            {QStringLiteral("$os"), oneOf({QStringLiteral("windows"), QStringLiteral("linux"), QStringLiteral("other")})},
            {QStringLiteral("$os_version"), pattern(QStringLiteral("^[A-Za-z0-9._-]{1,24}$"))},
            {QStringLiteral("arch"), oneOf({QStringLiteral("x86_64"), QStringLiteral("arm64"), QStringLiteral("other")})},
            {QStringLiteral("package"),
             oneOf({QStringLiteral("installer"), QStringLiteral("portable"), QStringLiteral("appimage"),
                    QStringLiteral("deb"), QStringLiteral("rpm"), QStringLiteral("aur"), QStringLiteral("build_tree")})},
            {QStringLiteral("qt_version"), pattern(QStringLiteral("^[0-9]{1,2}\\.[0-9]{1,3}\\.[0-9]{1,3}$"))},
            {QStringLiteral("language"), oneOf(kLanguages)},
            {QStringLiteral("theme"),
             oneOf({QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("high_contrast_white"),
                    QStringLiteral("high_contrast_black")})},
            {QStringLiteral("theme_follows_system"), {Rule::Bool, {}, {}}},
            {QStringLiteral("layout"), oneOf(kLayouts + QStringList{QStringLiteral("custom")})},
            {QStringLiteral("screens"), oneOf({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3+")})},
            {QStringLiteral("scale"),
             oneOf({QStringLiteral("1x"), QStringLiteral("1.25x"), QStringLiteral("1.5x"), QStringLiteral("2x"),
                    QStringLiteral("3x+")})},
            {QStringLiteral("startup_time"), oneOf(kTimeBuckets)},
        };
        t.emplace(QStringLiteral("app_started"), started);
        std::map<QString, Rule> closed{{QStringLiteral("session_length"), oneOf(kSessionBuckets)}};
        for (const char *bucket : Analytics::kFrameBuckets)
            closed.emplace(QStringLiteral("render_frames_") + QLatin1String(bucket), Rule{Rule::Count, {}, {}});
        t.emplace(QStringLiteral("app_closed"), std::move(closed));
        t.emplace(QStringLiteral("command_used"),
                  std::map<QString, Rule>{{QStringLiteral("command"), pattern(QStringLiteral("^[A-Z][A-Z0-9_]{2,60}$"))},
                                          {QStringLiteral("via"), oneOf({QStringLiteral("key"), QStringLiteral("pointer")})}});
        QStringList panels;
        for (const auto &[dock, id] : kPanels)
            panels << id;
        t.emplace(QStringLiteral("panel_opened"), std::map<QString, Rule>{{QStringLiteral("panel"), oneOf(panels)}});
        t.emplace(QStringLiteral("dialog_opened"), std::map<QString, Rule>{{QStringLiteral("dialog"), oneOf(kDialogs)}});
        t.emplace(QStringLiteral("layout_changed"), std::map<QString, Rule>{{QStringLiteral("layout"), oneOf(kLayouts)}});
        t.emplace(QStringLiteral("getting_started_finished"),
                  std::map<QString, Rule>{{QStringLiteral("pages_changed"), {Rule::ListOf, kPages, {}}}});
        t.emplace(QStringLiteral("getting_started_skipped"), std::map<QString, Rule>{});
        t.emplace(QStringLiteral("automation_error"),
                  std::map<QString, Rule>{
                      {QStringLiteral("kind"),
                       oneOf({QStringLiteral("load_failed"), QStringLiteral("failed"), QStringLiteral("lost"),
                              QStringLiteral("stopped")})},
                      {QStringLiteral("script"),
                       pattern(QStringLiteral("^(user script|[A-Za-z0-9_.-]{1,64}\\.(lua|moon))$"))}});
        std::map<QString, Rule> media{
            {QStringLiteral("open_time"), oneOf(kTimeBuckets)},
            {QStringLiteral("outcome"), oneOf({QStringLiteral("ready"), QStringLiteral("failed")})},
            {QStringLiteral("kind"), oneOf({QStringLiteral("file"), QStringLiteral("dummy")})}};
        t.emplace(QStringLiteral("video_opened"), media);
        media[QStringLiteral("kind")] = oneOf({QStringLiteral("file"), QStringLiteral("from_video"), QStringLiteral("dummy")});
        t.emplace(QStringLiteral("audio_opened"), media);
        t.emplace(QStringLiteral("$exception"),
                  std::map<QString, Rule>{{QStringLiteral("$exception_list"), {Rule::Exceptions, {}, {}}},
                                          {QStringLiteral("$exception_level"),
                                           oneOf({QStringLiteral("fatal"), QStringLiteral("error")})},
                                          {QStringLiteral("crashed_version"), pattern(kVersion)},
                                          {QStringLiteral("report_id"), pattern(kUuid)},
                                          {QStringLiteral("freeze_seconds"), {Rule::Count, {}, {}}}});
        std::map<QString, Rule> report{
            {QStringLiteral("report_id"), pattern(kUuid)},
            {QStringLiteral("kind"), oneOf({QStringLiteral("crash"), QStringLiteral("freeze")})},
            {QStringLiteral("note"), {Rule::Text, {}, {}, 4000}},
            {QStringLiteral("log"), {Rule::Text, {}, {}, 64 * 1024}},
            {QStringLiteral("settings"), {Rule::Settings, {}, {}, 64}},
        };
        for (const char *key : {"version", "channel", "$os", "$os_version", "arch", "package", "qt_version"})
            report.emplace(QString::fromLatin1(key), started.at(QString::fromLatin1(key)));
        t.emplace(QStringLiteral("crash_report"), std::move(report));
        return t;
    }();
    return events;
}

bool matches(const QString &value, const QString &p)
{
    return QRegularExpression(p).match(value).hasMatch();
}

bool exceptionsConform(const QJsonValue &value)
{
    static const QString type = QStringLiteral("^(SIG[A-Z]{2,6}|[A-Z_]{4,40}|0x[0-9a-f]{1,8})$");
    static const QString module = QStringLiteral("^[A-Za-z0-9_.+-]{1,80}$");
    static const QString offset = QStringLiteral("^0x[0-9a-f]{1,16}$");
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() != 1)
        return false;
    const QJsonObject e = list.first().toObject();
    if (e.keys() != QStringList{QStringLiteral("mechanism"), QStringLiteral("stacktrace"), QStringLiteral("type"),
                                QStringLiteral("value")} ||
        !matches(e.value(QStringLiteral("type")).toString(), type) || e.value(QStringLiteral("value")) != e.value(QStringLiteral("type")))
        return false;
    const QJsonObject mechanism = e.value(QStringLiteral("mechanism")).toObject();
    if (mechanism.keys() != QStringList{QStringLiteral("handled"), QStringLiteral("type")} ||
        mechanism.value(QStringLiteral("handled")) != QJsonValue(false) ||
        !(mechanism.value(QStringLiteral("type")) == QJsonValue(QStringLiteral("signal")) ||
          mechanism.value(QStringLiteral("type")) == QJsonValue(QStringLiteral("seh")) ||
          mechanism.value(QStringLiteral("type")) == QJsonValue(QStringLiteral("watchdog"))))
        return false;
    const QJsonObject stack = e.value(QStringLiteral("stacktrace")).toObject();
    if (stack.keys() != QStringList{QStringLiteral("frames"), QStringLiteral("type")} ||
        stack.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("raw")))
        return false;
    const QJsonArray frames = stack.value(QStringLiteral("frames")).toArray();
    if (frames.size() > crash::kMaxFrames)
        return false;
    for (const QJsonValue &f : frames) {
        const QJsonObject frame = f.toObject();
        if (frame.keys() != QStringList{QStringLiteral("module"), QStringLiteral("offset"), QStringLiteral("platform")} ||
            frame.value(QStringLiteral("platform")) != QJsonValue(QStringLiteral("native")) ||
            !matches(frame.value(QStringLiteral("module")).toString(), module) ||
            !matches(frame.value(QStringLiteral("offset")).toString(), offset))
            return false;
    }
    return true;
}

bool conformsTo(const Rule &rule, const QJsonValue &value)
{
    switch (rule.kind) {
    case Rule::OneOf:
        return value.isString() && rule.values.contains(value.toString());
    case Rule::Pattern:
        return value.isString() && matches(value.toString(), rule.pattern);
    case Rule::Bool:
        return value.isBool();
    case Rule::Count:
        return value.isDouble() && value.toDouble() >= 0 && value.toDouble() <= 1e9 &&
               value.toDouble() == double(qint64(value.toDouble()));
    case Rule::ListOf: {
        if (!value.isArray() || value.toArray().size() > rule.values.size())
            return false;
        for (const QJsonValue &v : value.toArray())
            if (!v.isString() || !rule.values.contains(v.toString()))
                return false;
        return true;
    }
    case Rule::Exceptions:
        return exceptionsConform(value);
    case Rule::Text:
        return value.isString() && value.toString().size() <= rule.max;
    case Rule::Settings: {
        static const QRegularExpression id(QStringLiteral("^[a-z ]{1,32}$"));
        static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_.+, -]{1,64}$"));
        const QJsonObject o = value.toObject();
        if (!value.isObject() || o.size() > rule.max)
            return false;
        for (auto it = o.begin(); it != o.end(); ++it)
            if (!id.match(it.key()).hasMatch() || !it.value().isString() || !plain.match(it.value().toString()).hasMatch())
                return false;
        return true;
    }
    }
    return false;
}

QString themeId(const QString &code)
{
    if (code == QLatin1String("highContrastWhite"))
        return QStringLiteral("high_contrast_white");
    if (code == QLatin1String("highContrastBlack"))
        return QStringLiteral("high_contrast_black");
    return code == QLatin1String("light") ? code : QStringLiteral("dark");
}

QString os()
{
#ifdef _WIN32
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("other");
#endif
}

QString arch()
{
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if (cpu == QLatin1String("x86_64"))
        return cpu;
    return cpu == QLatin1String("arm64") ? cpu : QStringLiteral("other");
}

} // namespace

bool Analytics::doNotTrack(const QByteArray &value)
{
    return !value.isEmpty() && value != "0";
}

Analytics::Analytics(ui::SettingsStore &settings, Config config, QObject *parent)
    : QObject(parent), m_settings(settings), m_config(std::move(config)),
      m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_session.start();
    m_enabled = m_settings.boolean(kEnabled);
    m_counting = active();
    m_flushTimer.setInterval(m_config.flushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &Analytics::flush);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(5000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Analytics::save);
    connect(&m_settings, &ui::SettingsStore::changed, this, &Analytics::settingChanged);
    if (active()) {
        load();
        QTimer::singleShot(m_config.firstFlushMs, this, &Analytics::flush);
        m_flushTimer.start();
    } else {
        clear();
    }
}

Analytics::~Analytics()
{
    if (m_reportReply) {
        m_reportReply->disconnect(this);
        m_reportReply->abort();
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

bool Analytics::active() const
{
    return !m_config.key.isEmpty() && m_enabled && !m_config.doNotTrack;
}

void Analytics::settingChanged(const QString &id)
{
    if (id != QLatin1String(kEnabled))
        return;
    const bool was = active();
    m_enabled = m_settings.boolean(kEnabled);
    if (!m_enabled) {
        clear();
        m_settings.set(kId, QString());
    }
    if (was == active())
        return;
    m_counting = active();
    if (active()) {
        m_session.start();
        m_flushTimer.start();
    } else {
        m_flushTimer.stop();
    }
    emit activeChanged();
}

void Analytics::clear()
{
    m_queue = {};
    m_saveTimer.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
        m_inFlight = 0;
    }
    if (!m_config.queueFile.isEmpty())
        QFile::remove(m_config.queueFile);
    for (auto &count : m_frames)
        count = 0;
}

QString Analytics::distinctId() const
{
    return m_settings.text(kId);
}

QJsonObject Analytics::common() const
{
    QString id = m_settings.text(kId);
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.set(kId, id);
    }
    return {{QStringLiteral("distinct_id"), id},
            {QStringLiteral("$process_person_profile"), false},
            {QStringLiteral("$geoip_disable"), true},
            {QStringLiteral("$session_id"), m_sessionId},
            {QStringLiteral("$lib"), QStringLiteral("hikarisub")},
            {QStringLiteral("$lib_version"), m_config.version}};
}

bool Analytics::conforms(const QString &event, const QJsonObject &properties)
{
    const auto it = table().find(event);
    if (it == table().end())
        return false;
    for (auto p = properties.begin(); p != properties.end(); ++p) {
        const auto rule = it->second.find(p.key());
        if (rule == it->second.end() || !conformsTo(rule->second, p.value()))
            return false;
    }
    return true;
}

bool Analytics::capture(const QString &event, const QJsonObject &properties)
{
    if (!active())
        return false;
    if (!conforms(event, properties)) {
        qWarning("Analytics: %s does not conform; not sent", qPrintable(event));
        return false;
    }
    QJsonObject all = common();
    for (auto p = properties.begin(); p != properties.end(); ++p)
        all.insert(p.key(), p.value());
    enqueue({{QStringLiteral("event"), event},
             {QStringLiteral("distinct_id"), all.value(QStringLiteral("distinct_id"))},
             {QStringLiteral("properties"), all},
             {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    return true;
}

void Analytics::enqueue(QJsonObject event)
{
    m_queue.append(event);
    trim();
    if (!m_config.queueFile.isEmpty())
        m_saveTimer.start();
}

void Analytics::trim()
{
    while (m_queue.size() > kMaxEvents ||
           (m_queue.size() > 1 && QJsonDocument(m_queue).toJson(QJsonDocument::Compact).size() > kMaxBytes)) {
        if (m_inFlight < m_queue.size()) {
            m_queue.removeAt(m_inFlight);
        } else {
            m_queue.removeFirst();
            --m_inFlight;
        }
    }
}

void Analytics::load()
{
    if (m_config.queueFile.isEmpty())
        return;
    QFile file(m_config.queueFile);
    if (file.size() > kMaxBytes * 2 || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray saved = QJsonDocument::fromJson(file.readAll()).array();
    QJsonArray kept;
    for (const QJsonValue &v : saved) {
        const QJsonObject e = v.toObject();
        QJsonObject own = e.value(QStringLiteral("properties")).toObject();
        for (const QString &key : common().keys())
            own.remove(key);
        if (conforms(e.value(QStringLiteral("event")).toString(), own))
            kept.append(e);
    }
    for (const QJsonValue &v : std::as_const(m_queue))
        kept.append(v);
    m_queue = kept;
    trim();
}

void Analytics::save()
{
    if (m_config.queueFile.isEmpty() || !active())
        return;
    if (m_queue.isEmpty()) {
        QFile::remove(m_config.queueFile);
        return;
    }
    QSaveFile file(m_config.queueFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_queue).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

QJsonArray Analytics::queued() const
{
    return m_queue;
}

void Analytics::flush()
{
    if (!active() || m_reply || m_queue.isEmpty())
        return;
    m_inFlight = int(std::min<qsizetype>(m_queue.size(), kBatchSize));
    QJsonArray batch;
    for (int i = 0; i < m_inFlight; ++i)
        batch.append(m_queue.at(i));
    const QJsonObject body{{QStringLiteral("api_key"), m_config.key}, {QStringLiteral("batch"), batch}};
    QNetworkRequest request(m_config.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_config.version));
    request.setTransferTimeout(30 * 1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &Analytics::sendFinished);
}

void Analytics::sendFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // A 4xx other than 408/429 is permanent: drop the batch as if sent.
    const bool done = (status >= 200 && status < 300) || (status >= 400 && status < 500 && status != 408 && status != 429);
    const int count = done ? m_inFlight : 0;
    for (int i = 0; i < count && !m_queue.isEmpty(); ++i)
        m_queue.removeFirst();
    m_inFlight = 0;
    if (count) {
        if (!m_config.queueFile.isEmpty())
            m_saveTimer.start();
        emit sent(count);
    }
}

void Analytics::commandUsed(const QString &symbol, const QString &via)
{
    if (application::hotkeyIdOf(symbol.toStdString()) == 0)
        return;
    if (via != QLatin1String("key") && via != QLatin1String("pointer"))
        return;
    const QString once = QStringLiteral("command:") + symbol + QLatin1Char(':') + via;
    if (!active() || m_once.contains(once))
        return;
    m_once.insert(once);
    capture(QStringLiteral("command_used"), {{QStringLiteral("command"), symbol}, {QStringLiteral("via"), via}});
}

QString Analytics::panelId(const QString &dock)
{
    const auto it = kPanels.find(dock);
    return it == kPanels.end() ? QString() : it->second;
}

void Analytics::panelOpened(const QString &dock)
{
    const QString id = panelId(dock);
    if (id.isEmpty() || !active() || m_once.contains(QStringLiteral("panel:") + id))
        return;
    m_once.insert(QStringLiteral("panel:") + id);
    capture(QStringLiteral("panel_opened"), {{QStringLiteral("panel"), id}});
}

void Analytics::dialogOpened(const QString &id)
{
    const QString dialogId = id.startsWith(QLatin1String("automation.")) ? QStringLiteral("automation_script") : id;
    if (!kDialogs.contains(dialogId) || !active() || m_once.contains(QStringLiteral("dialog:") + dialogId))
        return;
    m_once.insert(QStringLiteral("dialog:") + dialogId);
    capture(QStringLiteral("dialog_opened"), {{QStringLiteral("dialog"), dialogId}});
}

void Analytics::layoutChanged(const QString &layout)
{
    const QString id = layout == QLatin1String("saved") ? layout : layout.toLower();
    if (!kLayouts.contains(id) || !active() || m_layoutChanges >= kMaxLayoutChanges)
        return;
    if (capture(QStringLiteral("layout_changed"), {{QStringLiteral("layout"), id}}))
        ++m_layoutChanges;
}

void Analytics::gettingStartedFinished(const QStringList &pagesChanged)
{
    QJsonArray pages;
    for (const QString &page : kPages)
        if (pagesChanged.contains(page))
            pages.append(page);
    capture(QStringLiteral("getting_started_finished"), {{QStringLiteral("pages_changed"), pages}});
}

void Analytics::gettingStartedSkipped()
{
    capture(QStringLiteral("getting_started_skipped"));
}

void Analytics::automationError(const QString &kind, const QString &bundledScript)
{
    capture(QStringLiteral("automation_error"),
            {{QStringLiteral("kind"), kind},
             {QStringLiteral("script"), bundledScript.isEmpty() ? QStringLiteral("user script")
                                                                : QString(bundledScript).replace(u' ', u'_')}});
}

QString Analytics::bundledScript(const QString &path, const QString &manifestPath)
{
    const QFileInfo script(path);
    const QString name = script.fileName();
    QFile manifest(manifestPath);
    QFile file(path);
    if (!manifest.open(QIODevice::ReadOnly) || !file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    const QByteArray digest = hash.result().toHex();
    for (const QByteArray &raw : manifest.readAll().split('\n')) {
        const QByteArray line = raw.trimmed();
        const qsizetype space = line.indexOf(' ');
        if (space > 0 && line.left(space).toLower() == digest && line.endsWith('/' + name.toUtf8()) &&
            line.mid(space + 1).startsWith("Automation/"))
            return name;
    }
    return {};
}

void Analytics::mediaOpened(bool video, qint64 milliseconds, bool ready, const QString &kind)
{
    capture(video ? QStringLiteral("video_opened") : QStringLiteral("audio_opened"),
            {{QStringLiteral("open_time"), timeBucket(milliseconds)},
             {QStringLiteral("outcome"), ready ? QStringLiteral("ready") : QStringLiteral("failed")},
             {QStringLiteral("kind"), kind}});
}

void Analytics::frameRendered(double milliseconds)
{
    if (!m_counting.load(std::memory_order_relaxed))
        return;
    m_frames[std::size_t(frameBucket(milliseconds))].fetch_add(1, std::memory_order_relaxed);
}

void Analytics::appStarted(const StartFacts &facts)
{
    if (m_started)
        return;
    m_started = true;
    const QString version = m_config.version;
    const QString language =
        Localisation::resolve(m_settings.text("program.language"), {QStringLiteral("ko_KR"), QStringLiteral("pl"),
                                                                    QStringLiteral("ta"), QStringLiteral("th_TH")});
    QString osVersion = QSysInfo::productVersion();
    if (!matches(osVersion, QStringLiteral("^[A-Za-z0-9._-]{1,24}$")))
        osVersion = QStringLiteral("other");
    const QString layout = kLayouts.contains(facts.layout.toLower()) ? facts.layout.toLower() : QStringLiteral("custom");
    capture(QStringLiteral("app_started"),
            {{QStringLiteral("version"), version},
             {QStringLiteral("channel"), version.contains(QLatin1Char('-')) ? QStringLiteral("prerelease") : QStringLiteral("stable")},
             {QStringLiteral("$os"), os()},
             {QStringLiteral("$os_version"), osVersion},
             {QStringLiteral("arch"), arch()},
             {QStringLiteral("package"), facts.package},
             {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
             {QStringLiteral("language"), language.isEmpty() ? QStringLiteral("en") : language},
             {QStringLiteral("theme"), themeId(m_settings.text(ui::theme::kThemeSetting))},
             {QStringLiteral("theme_follows_system"), m_settings.boolean("appearance.followSystem")},
             {QStringLiteral("layout"), layout},
             {QStringLiteral("screens"), screensBucket(facts.screens)},
             {QStringLiteral("scale"), scaleBucket(facts.scale)},
             {QStringLiteral("startup_time"), timeBucket(facts.startupMs)}});
}

bool Analytics::sendReport(const QList<std::pair<QString, QJsonObject>> &events)
{
    if (m_config.key.isEmpty() || m_reportReply || events.isEmpty())
        return false;
    for (const auto &[event, properties] : events)
        if (!conforms(event, properties)) {
            qWarning("Analytics: %s does not conform; not sent", qPrintable(event));
            return false;
        }
    QJsonObject base = active() ? common()
                                : QJsonObject{{QStringLiteral("distinct_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                              {QStringLiteral("$process_person_profile"), false},
                                              {QStringLiteral("$geoip_disable"), true},
                                              {QStringLiteral("$lib"), QStringLiteral("hikarisub")},
                                              {QStringLiteral("$lib_version"), m_config.version}};
    QJsonArray batch;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (const auto &[event, properties] : events) {
        QJsonObject all = base;
        for (auto p = properties.begin(); p != properties.end(); ++p)
            all.insert(p.key(), p.value());
        batch.append(QJsonObject{{QStringLiteral("event"), event},
                                 {QStringLiteral("distinct_id"), all.value(QStringLiteral("distinct_id"))},
                                 {QStringLiteral("properties"), all},
                                 {QStringLiteral("timestamp"), now}});
    }
    const QJsonObject body{{QStringLiteral("api_key"), m_config.key}, {QStringLiteral("batch"), batch}};
    QNetworkRequest request(m_config.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_config.version));
    request.setTransferTimeout(30 * 1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_reportReply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reportReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reportReply;
        m_reportReply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit reportSent(status >= 200 && status < 300);
    });
    return true;
}

void Analytics::quit(int timeoutMs)
{
    if (!active())
        return;
    if (!m_closed) {
        m_closed = true;
        QJsonObject closed{{QStringLiteral("session_length"), sessionBucket(m_session.elapsed() / 1000)}};
        for (std::size_t i = 0; i < kFrameBuckets.size(); ++i)
            closed.insert(QStringLiteral("render_frames_") + QLatin1String(kFrameBuckets[i]), m_frames[i].load());
        capture(QStringLiteral("app_closed"), closed);
    }
    m_saveTimer.stop();
    if (!m_reply)
        flush();
    if (m_reply) {
        QEventLoop loop;
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        connect(m_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit, Qt::QueuedConnection);
        loop.exec();
        if (m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
            m_inFlight = 0;
        }
    }
    save();
}

QString Analytics::osId()
{
    return os();
}

QString Analytics::archId()
{
    return arch();
}

QString Analytics::timeBucket(qint64 ms)
{
    const qint64 limits[] = {250, 500, 1000, 2000, 5000, 15000, 60000};
    for (int i = 0; i < 7; ++i)
        if (ms < limits[i])
            return kTimeBuckets[i];
    return kTimeBuckets.last();
}

QString Analytics::sessionBucket(qint64 seconds)
{
    const qint64 limits[] = {60, 300, 900, 3600, 3 * 3600};
    for (int i = 0; i < 5; ++i)
        if (seconds < limits[i])
            return kSessionBuckets[i];
    return kSessionBuckets.last();
}

QString Analytics::scaleBucket(qreal scale)
{
    if (scale < 1.125)
        return QStringLiteral("1x");
    if (scale < 1.375)
        return QStringLiteral("1.25x");
    if (scale < 1.75)
        return QStringLiteral("1.5x");
    return scale < 2.5 ? QStringLiteral("2x") : QStringLiteral("3x+");
}

QString Analytics::screensBucket(int screens)
{
    if (screens <= 1)
        return QStringLiteral("1");
    return screens == 2 ? QStringLiteral("2") : QStringLiteral("3+");
}

int Analytics::frameBucket(double ms)
{
    const double limits[] = {8, 16, 33, 66, 200};
    for (int i = 0; i < 5; ++i)
        if (ms < limits[i])
            return i;
    return 5;
}

} // namespace hikari::app
