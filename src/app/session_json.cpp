#include "hikari/app/session_json.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace hikari::app {

namespace {

constexpr std::int64_t kVersion = 1;

QString qs(const std::string &s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

// `text` is marked where it is written (QT_TRANSLATE_NOOP), for lupdate.
std::string message(const char *text, const QStringList &args = {})
{
    QString m = QCoreApplication::translate("HikariSub.Session", text);
    for (const QString &arg : args)
        m = m.arg(arg);
    return m.toStdString();
}

// After a UTF-8 BOM and blanks, a JSON file starts with "{"; a legacy file
// with "[HikariSub" (or another encoding's BOM).
bool isJson(std::string_view bytes)
{
    if (bytes.starts_with("\xEF\xBB\xBF"))
        bytes.remove_prefix(3);
    const auto first = bytes.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && bytes[first] == '{';
}

std::optional<application::Session> readLegacy(std::string_view bytes, application::ParseFailure *failure)
{
    const auto text = application::decodeSessionBytes(bytes);
    if (!text)
        return std::nullopt;
    auto session = application::parseSession(*text);
    if (!session && failure)
        *failure = {0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "Session file is corrupt"))};
    if (session && !session->tabs.empty())
        session->activeTab = session->tabs.size() - 1; // AddPage made each loaded tab active
    return session;
}

std::optional<application::Session> readJson(std::string_view bytes, application::ParseFailure *failure)
{
    auto fail = [&](application::ParseFailure f) -> std::optional<application::Session> {
        if (failure)
            *failure = std::move(f);
        return std::nullopt;
    };
    QJsonParseError error{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())), &error);
    if (error.error != QJsonParseError::NoError) {
        const auto [line, column] = application::lineColumnAt(bytes, static_cast<std::size_t>(error.offset));
        return fail({line, column, error.errorString().toStdString()});
    }
    if (!doc.isObject())
        return fail({1, 1, message(QT_TRANSLATE_NOOP("HikariSub.Session", "it is not a session file"))});
    const QJsonObject o = doc.object();
    const QJsonValue version = o.value(QStringLiteral("version"));
    std::optional<std::int64_t> number;
    if (version.isDouble() && version.toDouble() == static_cast<double>(version.toInteger()))
        number = version.toInteger();
    if (auto problem = application::checkFileVersion(number, kVersion))
        return fail(std::move(*problem));
    if (!o.value(QStringLiteral("tabs")).isArray())
        return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "\"tabs\" is not a list"))});

    application::Session session;
    session.header = o.value(QStringLiteral("program")).toString().toStdString();
    const QJsonValue closed = o.value(QStringLiteral("closed"));
    if (!closed.isBool() && !closed.isUndefined())
        return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "\"closed\" is not true or false"))});
    session.closed = closed.toBool(true);
    const QJsonArray tabs = o.value(QStringLiteral("tabs")).toArray();
    for (qsizetype i = 0; i < tabs.size(); ++i) {
        const QString where = QString::number(i + 1);
        if (!tabs[i].isObject())
            return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "tab %1 is not an object"), {where})});
        const QJsonObject t = tabs[i].toObject();
        application::SessionTab tab;
        // A field of the wrong type makes the file broken; a missing one is empty.
        std::string wrong;
        auto text = [&](const char *key, std::string &out) {
            const QJsonValue v = t.value(QLatin1String(key));
            if (v.isString())
                out = v.toString().toStdString();
            else if (!v.isUndefined() && wrong.empty())
                wrong = key;
        };
        auto integer = [&](const char *key, int &out) {
            const QJsonValue v = t.value(QLatin1String(key));
            if (v.isDouble() && v.toDouble() == static_cast<double>(v.toInt()))
                out = v.toInt();
            else if (!v.isUndefined() && wrong.empty())
                wrong = key;
        };
        text("subtitles", tab.subtitles);
        text("video", tab.video);
        integer("position", tab.position);
        text("audio", tab.audio);
        text("keyframes", tab.keyframes);
        integer("activeLine", tab.active);
        integer("scroll", tab.scroll);
        if (const QJsonValue layout = t.value(QStringLiteral("layout")); layout.isObject())
            tab.layout = QJsonDocument(layout.toObject()).toJson(QJsonDocument::Compact).toStdString();
        else if (!layout.isUndefined() && wrong.empty())
            wrong = "layout";
        const QJsonValue active = t.value(QStringLiteral("active"));
        if (!active.isBool() && !active.isUndefined() && wrong.empty())
            wrong = "active";
        if (!wrong.empty())
            return fail({0, 0,
                         message(QT_TRANSLATE_NOOP("HikariSub.Session", "tab %1: \"%2\" has the wrong type"),
                                 {where, QString::fromStdString(wrong)})});
        if (active.toBool() && !session.activeTab)
            session.activeTab = static_cast<std::size_t>(i);
        session.tabs.push_back(std::move(tab));
    }
    return session;
}

} // namespace

QByteArray writeSessionJson(const application::Session &session, std::optional<bool> closed)
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), static_cast<qint64>(kVersion));
    o.insert(QStringLiteral("program"), qs(session.header));
    if (closed)
        o.insert(QStringLiteral("closed"), *closed);
    QJsonArray tabs;
    for (std::size_t i = 0; i < session.tabs.size(); ++i) {
        const auto &tab = session.tabs[i];
        QJsonObject t;
        if (session.activeTab == i)
            t.insert(QStringLiteral("active"), true);
        t.insert(QStringLiteral("subtitles"), qs(tab.subtitles));
        t.insert(QStringLiteral("video"), qs(tab.video));
        t.insert(QStringLiteral("position"), tab.position);
        if (!tab.audio.empty() && tab.audio != tab.video)
            t.insert(QStringLiteral("audio"), qs(tab.audio));
        if (!tab.keyframes.empty())
            t.insert(QStringLiteral("keyframes"), qs(tab.keyframes));
        t.insert(QStringLiteral("activeLine"), tab.active);
        t.insert(QStringLiteral("scroll"), tab.scroll);
        // A layout that is not an object (a .kls's damaged line) has nothing to restore.
        if (const QJsonDocument layout = QJsonDocument::fromJson(QByteArray::fromStdString(tab.layout));
            layout.isObject())
            t.insert(QStringLiteral("layout"), layout.object());
        tabs.append(t);
    }
    o.insert(QStringLiteral("tabs"), tabs);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

std::optional<application::Session> readSession(std::string_view bytes, application::ParseFailure *failure)
{
    return isJson(bytes) ? readJson(bytes, failure) : readLegacy(bytes, failure);
}

} // namespace hikari::app
