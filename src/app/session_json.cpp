#include "hikari/app/session_json.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace hikari::app {

namespace {

constexpr std::int64_t kVersion = 1;

QString qs(const std::string &s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

// Callers mark `text` with QT_TRANSLATE_NOOP so lupdate finds it.
std::string message(const char *text, const QStringList &args = {})
{
    QString m = QCoreApplication::translate("HikariSub.Session", text);
    for (const QString &arg : args)
        m = m.arg(arg);
    return m.toStdString();
}

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
        *failure = {0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "Session file is corrupt."))};
    if (session && !session->tabs.empty())
        session->activeTab = session->tabs.size() - 1;
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
        return fail({line, column,
                     message(QT_TRANSLATE_NOOP("HikariSub.Session", "It is not valid JSON: %1."), {error.errorString()})});
    }
    if (!doc.isObject())
        return fail({1, 1, message(QT_TRANSLATE_NOOP("HikariSub.Session", "It is not a session file."))});
    const QJsonObject o = doc.object();
    const QJsonValue version = o.value(QStringLiteral("version"));
    std::optional<std::int64_t> number;
    if (version.isDouble() && version.toDouble() == static_cast<double>(version.toInteger()))
        number = version.toInteger();
    if (auto problem = application::checkFileVersion(number, kVersion))
        return fail(std::move(*problem));
    if (!o.value(QStringLiteral("tabs")).isArray())
        return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "“tabs” is not a list."))});

    application::Session session;
    session.header = o.value(QStringLiteral("program")).toString().toStdString();
    const QJsonValue closed = o.value(QStringLiteral("closed"));
    if (!closed.isBool() && !closed.isUndefined())
        return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "“closed” is not true or false."))});
    session.closed = closed.toBool(true);
    if (const QJsonValue windows = o.value(QStringLiteral("windows")); !windows.isUndefined()) {
        if (!windows.isArray())
            return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "“windows” is not a list."))});
        for (const QJsonValue &value : windows.toArray()) {
            const QString where = QString::number(session.windows.size() + 1);
            auto wrongType = [&](const char *key) {
                return fail({0, 0,
                             message(QT_TRANSLATE_NOOP("HikariSub.Session", "Window %1: “%2” has the wrong type."),
                                     {where, QLatin1String(key)})});
            };
            if (!value.isObject())
                return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "Window %1 is not an object."), {where})});
            const QJsonObject w = value.toObject();
            application::SessionWindow window;
            application::SessionGeometry geometry;
            int found = 0;
            for (auto [key, out] : {std::pair{"x", &geometry.x}, std::pair{"y", &geometry.y},
                                    std::pair{"width", &geometry.width}, std::pair{"height", &geometry.height}}) {
                const QJsonValue v = w.value(QLatin1String(key));
                if (v.isUndefined())
                    continue;
                if (!v.isDouble() || v.toDouble() != static_cast<double>(v.toInt()))
                    return wrongType(key);
                *out = v.toInt();
                ++found;
            }
            if (found == 4 && geometry.width > 0 && geometry.height > 0)
                window.geometry = geometry;
            const QJsonValue maximized = w.value(QStringLiteral("maximized"));
            if (!maximized.isBool() && !maximized.isUndefined())
                return wrongType("maximized");
            window.maximized = maximized.toBool();
            QJsonObject unknown = w;
            for (const char *key : {"x", "y", "width", "height", "maximized"})
                unknown.remove(QLatin1String(key));
            if (!unknown.isEmpty())
                window.unknown = QJsonDocument(unknown).toJson(QJsonDocument::Compact).toStdString();
            session.windows.push_back(window);
        }
    }
    const QJsonArray tabs = o.value(QStringLiteral("tabs")).toArray();
    for (qsizetype i = 0; i < tabs.size(); ++i) {
        const QString where = QString::number(i + 1);
        if (!tabs[i].isObject())
            return fail({0, 0, message(QT_TRANSLATE_NOOP("HikariSub.Session", "Tab %1 is not an object."), {where})});
        const QJsonObject t = tabs[i].toObject();
        application::SessionTab tab;
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
        int window = 0;
        integer("window", window);
        if (window < 0 && wrong.empty())
            wrong = "window";
        const QJsonValue current = t.value(QStringLiteral("current"));
        if (!current.isBool() && !current.isUndefined() && wrong.empty())
            wrong = "current";
        if (!wrong.empty())
            return fail({0, 0,
                         message(QT_TRANSLATE_NOOP("HikariSub.Session", "Tab %1: “%2” has the wrong type."),
                                 {where, QString::fromStdString(wrong)})});
        const auto index = static_cast<std::size_t>(i);
        if (active.toBool() && !session.activeTab)
            session.activeTab = index;
        if (static_cast<std::size_t>(window) < session.windows.size()) {
            tab.window = static_cast<std::size_t>(window);
            if (current.toBool() && !session.windows[tab.window].currentTab)
                session.windows[tab.window].currentTab = index;
        }
        session.tabs.push_back(std::move(tab));
    }
    return session;
}

}

QByteArray writeSessionJson(const application::Session &session, std::optional<bool> closed)
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), static_cast<qint64>(kVersion));
    o.insert(QStringLiteral("program"), qs(session.header));
    if (closed)
        o.insert(QStringLiteral("closed"), *closed);
    if (!session.windows.empty()) {
        QJsonArray windows;
        for (const auto &window : session.windows) {
            QJsonObject w = QJsonDocument::fromJson(QByteArray::fromStdString(window.unknown)).object();
            if (const auto &g = window.geometry) {
                w.insert(QStringLiteral("x"), g->x);
                w.insert(QStringLiteral("y"), g->y);
                w.insert(QStringLiteral("width"), g->width);
                w.insert(QStringLiteral("height"), g->height);
            }
            w.insert(QStringLiteral("maximized"), window.maximized);
            windows.append(w);
        }
        o.insert(QStringLiteral("windows"), windows);
    }
    QJsonArray tabs;
    for (std::size_t i = 0; i < session.tabs.size(); ++i) {
        const auto &tab = session.tabs[i];
        QJsonObject t;
        if (session.activeTab == i)
            t.insert(QStringLiteral("active"), true);
        if (tab.window > 0)
            t.insert(QStringLiteral("window"), static_cast<qint64>(tab.window));
        if (std::ranges::any_of(session.windows, [i](const auto &w) { return w.currentTab == i; }))
            t.insert(QStringLiteral("current"), true);
        t.insert(QStringLiteral("subtitles"), qs(tab.subtitles));
        t.insert(QStringLiteral("video"), qs(tab.video));
        t.insert(QStringLiteral("position"), tab.position);
        if (!tab.audio.empty() && tab.audio != tab.video)
            t.insert(QStringLiteral("audio"), qs(tab.audio));
        if (!tab.keyframes.empty())
            t.insert(QStringLiteral("keyframes"), qs(tab.keyframes));
        t.insert(QStringLiteral("activeLine"), tab.active);
        t.insert(QStringLiteral("scroll"), tab.scroll);
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

}
