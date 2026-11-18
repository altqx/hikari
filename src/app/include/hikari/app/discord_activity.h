#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <array>
#include <functional>
#include <optional>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app::discord {

// When activities overlap, the first in this order shows.
enum class Activity { Watching, Effects, Styling, Spelling, Reviewing, Exporting, Agent };
inline constexpr std::array kActivityOrder{Activity::Watching,  Activity::Effects,   Activity::Styling,
                                           Activity::Spelling,  Activity::Reviewing, Activity::Exporting,
                                           Activity::Agent};
QString activityKey(Activity activity);
QString activityLabel(Activity activity);
std::optional<Activity> activityFromKey(const QString &key);

// The privacy.discord* choices; from the store, or from the Settings window's staged values.
struct Choices {
    bool mode = true;
    bool modeIcon = true;
    bool fileName = false;
    bool projectName = false;
    QStringList activities;
    bool ownText = false;
    QString details;
    QString state;
    QString plugin;
    QString time = QStringLiteral("started");
    QString idle = QStringLiteral("idle");
    bool getButton = true;
    bool link = false;
    QString linkLabel;
    QString linkUrl;

    static Choices read(const std::function<QVariant(const QString &id)> &value);
    static Choices fromStore(const ui::SettingsStore &store);
    static Choices fromMap(const QVariantMap &values);
};

// The current Project's choice on its Project tab; Own only once its folder is trusted.
struct ProjectChoice {
    enum Kind { Yours, Hide, Own } kind = Yours;
    QString show;
    QString details;
    QString state;
};

struct Scene {
    bool tabOpen = false;
    QString layoutMode = QStringLiteral("Editing"); // a workspace preset
    std::optional<Activity> activity;               // the one that shows, after the user's unticks
    QString fileName;
    QString title;       // Script Info Title
    QString projectName; // the Project's folder name
    ProjectChoice project;
    QString pluginText; // from the plugin the user picked
    bool idle = false;
    std::optional<qint64> since; // seconds since the epoch; none: no time
};

// The shown activity among `going`, honouring the user's unticks; none: the layout's mode.
std::optional<Activity> shownActivity(const Choices &choices, const std::function<bool(Activity)> &going);
QString episodeIn(const QString &fileName);
bool linkUrlValid(const QString &url);
// What goes to Discord; empty clears the status.
QJsonObject compose(const Choices &choices, const Scene &scene);

}
