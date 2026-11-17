#include "hikari/app/discord_activity.h"

#include "settings_store.h"
#include "workspace_layout.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

namespace hikari::app::discord {

namespace {

constexpr qsizetype kMaxText = 128;
constexpr qsizetype kMinText = 2;
constexpr qsizetype kMaxLabel = 32;

QString fit(QString text, qsizetype most)
{
    text = text.trimmed();
    if (text.size() > most)
        text = text.left(most - 1) + QChar(0x2026);
    return text;
}

QString expand(const QString &line, const QHash<QString, QString> &fills)
{
    static const QRegularExpression placeholder(QStringLiteral("\\{([a-z]+)\\}"));
    QString out;
    qsizetype from = 0;
    for (auto it = placeholder.globalMatch(line); it.hasNext();) {
        const auto match = it.next();
        out += line.mid(from, match.capturedStart() - from);
        const auto fill = fills.constFind(match.captured(1));
        out += fill == fills.cend() ? match.captured() : *fill;
        from = match.capturedEnd();
    }
    return out + line.mid(from);
}

}

QString activityKey(Activity activity)
{
    switch (activity) {
    case Activity::Watching: return QStringLiteral("watching");
    case Activity::Effects: return QStringLiteral("effects");
    case Activity::Styling: return QStringLiteral("styling");
    case Activity::Spelling: return QStringLiteral("spelling");
    case Activity::Reviewing: return QStringLiteral("reviewing");
    case Activity::Exporting: return QStringLiteral("exporting");
    case Activity::Agent: return QStringLiteral("agent");
    }
    return {};
}

QString activityLabel(Activity activity)
{
    switch (activity) {
    case Activity::Watching: return QCoreApplication::translate("HikariSub.Discord", "Watching");
    case Activity::Effects: return QCoreApplication::translate("HikariSub.Discord", "Effects");
    case Activity::Styling: return QCoreApplication::translate("HikariSub.Discord", "Styling");
    case Activity::Spelling: return QCoreApplication::translate("HikariSub.Discord", "Spell-checking");
    case Activity::Reviewing: return QCoreApplication::translate("HikariSub.Discord", "Reviewing");
    case Activity::Exporting: return QCoreApplication::translate("HikariSub.Discord", "Exporting");
    case Activity::Agent: return QCoreApplication::translate("HikariSub.Discord", "With the AI agent");
    }
    return {};
}

std::optional<Activity> activityFromKey(const QString &key)
{
    for (const Activity activity : kActivityOrder)
        if (activityKey(activity) == key)
            return activity;
    return std::nullopt;
}

Choices Choices::read(const std::function<QVariant(const QString &id)> &value)
{
    const auto text = [&](const char *id) { return value(QLatin1String(id)).toString(); };
    const auto on = [&](const char *id) { return value(QLatin1String(id)).toBool(); };
    Choices c;
    c.mode = on("privacy.discordMode");
    c.modeIcon = on("privacy.discordModeIcon");
    c.fileName = on("privacy.discordFileName");
    c.projectName = on("privacy.discordProjectName");
    c.activities = value(QStringLiteral("privacy.discordActivities")).toStringList();
    c.ownText = text("privacy.discordText") == QLatin1String("own");
    c.details = text("privacy.discordDetails");
    c.state = text("privacy.discordState");
    c.plugin = text("privacy.discordPlugin");
    c.time = text("privacy.discordTime");
    c.idle = text("privacy.discordIdle");
    c.getButton = on("privacy.discordGetButton");
    c.link = on("privacy.discordLink");
    c.linkLabel = text("privacy.discordLinkLabel");
    c.linkUrl = text("privacy.discordLinkUrl");
    return c;
}

Choices Choices::fromStore(const ui::SettingsStore &store)
{
    return read([&store](const QString &id) { return store.value(id); });
}

Choices Choices::fromMap(const QVariantMap &values)
{
    return read([&values](const QString &id) { return values.value(id); });
}

std::optional<Activity> shownActivity(const Choices &choices, const std::function<bool(Activity)> &going)
{
    for (const Activity activity : kActivityOrder)
        if (choices.activities.contains(activityKey(activity)) && going(activity))
            return activity;
    return std::nullopt;
}

QString episodeIn(const QString &fileName)
{
    static const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral("S\\d{1,3}E(\\d{1,4})"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("(?<![A-Za-z])E(\\d{1,4})(?!\\d)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(" - (\\d{1,4})(?!\\d)")),
    };
    for (const auto &pattern : patterns)
        if (const auto match = pattern.match(fileName); match.hasMatch())
            return match.captured(1);
    return {};
}

bool linkUrlValid(const QString &url)
{
    const QUrl parsed(url, QUrl::StrictMode);
    return url.startsWith(QLatin1String("https://")) && parsed.isValid() && !parsed.host().isEmpty();
}

QJsonObject compose(const Choices &choices, const Scene &scene)
{
    if (scene.project.kind == ProjectChoice::Hide || (scene.idle && choices.idle == QLatin1String("clear")))
        return {};
    const QJsonObject large{{QStringLiteral("large_image"), QStringLiteral("hikari")},
                            {QStringLiteral("large_text"), QStringLiteral("HikariSub")}};
    if (scene.idle && choices.idle != QLatin1String("keep"))
        return {{QStringLiteral("details"), QCoreApplication::translate("HikariSub.Discord", "Idle")}, {QStringLiteral("assets"), large}};

    QString modeKey, modeLabel, details, state;
    if (!scene.tabOpen) {
        details = QCoreApplication::translate("HikariSub.Discord", "In HikariSub");
        state = QCoreApplication::translate("HikariSub.Discord", "Home");
    } else {
        modeKey = scene.activity ? activityKey(*scene.activity) : scene.layoutMode.toLower();
        modeLabel = scene.activity ? activityLabel(*scene.activity)
                                   : ui::WorkspaceLayoutController::presetLabel(scene.layoutMode);
        const QString file = choices.fileName ? scene.fileName : QString();
        const QString project = choices.projectName ? scene.projectName : QString();
        const QHash<QString, QString> fills{
            {QStringLiteral("mode"), choices.mode ? modeLabel : QString()},
            {QStringLiteral("file"), file},
            {QStringLiteral("project"), project},
            {QStringLiteral("show"), scene.project.show.isEmpty() ? scene.title : scene.project.show},
            {QStringLiteral("episode"), episodeIn(file)},
            {QStringLiteral("title"), scene.title},
            {QStringLiteral("plugin"), scene.pluginText},
        };
        QStringList parts;
        for (const QString &part : {fills.value(QStringLiteral("mode")), file, project})
            if (!part.isEmpty())
                parts << part;
        const QString standardDetails = QCoreApplication::translate("HikariSub.Discord", "Editing subtitles");
        const QString standardState = parts.join(QStringLiteral(" · "));
        const auto line = [&](const QString &project, const QString &own, const QString &standard) {
            if (scene.project.kind == ProjectChoice::Own && !project.isEmpty())
                return expand(project, fills);
            return choices.ownText ? expand(own, fills) : standard;
        };
        details = line(scene.project.details, choices.details, standardDetails);
        state = line(scene.project.state, choices.state, standardState);
    }

    QJsonObject activity;
    if (details = fit(details, kMaxText); details.size() >= kMinText)
        activity.insert(QStringLiteral("details"), details);
    if (state = fit(state, kMaxText); state.size() >= kMinText)
        activity.insert(QStringLiteral("state"), state);
    if (scene.since)
        activity.insert(QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), *scene.since}});
    QJsonObject assets = large;
    if (choices.modeIcon && !modeKey.isEmpty()) {
        assets.insert(QStringLiteral("small_image"), modeKey);
        assets.insert(QStringLiteral("small_text"), modeLabel);
    }
    activity.insert(QStringLiteral("assets"), assets);
    QJsonArray buttons;
    if (choices.getButton)
        buttons.append(QJsonObject{{QStringLiteral("label"), QCoreApplication::translate("HikariSub.Discord", "Get HikariSub")},
                                   {QStringLiteral("url"), QStringLiteral("https://hikarisub.org")}});
    if (const QString label = fit(choices.linkLabel, kMaxLabel);
        choices.link && !label.isEmpty() && linkUrlValid(choices.linkUrl))
        buttons.append(QJsonObject{{QStringLiteral("label"), label}, {QStringLiteral("url"), choices.linkUrl}});
    if (!buttons.isEmpty())
        activity.insert(QStringLiteral("buttons"), buttons);
    return activity;
}

}
