#include "side_bar_model.h"

#include "hikari/application/side_bars.h"

#include <QVariantList>

#include <algorithm>
#include <utility>

namespace hikari::ui {
namespace {

using application::SideBarId;
using application::SideBars;

SideBars parsed(const QString &json)
{
    return application::parseSideBars(json.toStdString()).value_or(application::defaultSideBars({}));
}

QString text(const SideBars &bars) { return QString::fromStdString(application::sideBarsJson(bars)); }

bool firstStartEmpty = false;

SideBarId id(bool second) { return second ? SideBarId::Second : SideBarId::First; }

QVariantMap barMap(const application::SideBar &bar)
{
    QVariantList panels;
    for (const auto &panel : bar.panels)
        panels.append(QString::fromStdString(panel));
    return {{QStringLiteral("panels"), panels},
            {QStringLiteral("shown"), QString::fromStdString(bar.shown)},
            {QStringLiteral("width"), bar.width},
            {QStringLiteral("hidden"), bar.hidden}};
}

}

int SideBarModel::minimumWidth() const { return application::kSideBarMinimumWidth; }

bool SideBarModel::setFirstStartEmpty(bool empty) { return std::exchange(firstStartEmpty, empty); }

QString SideBarModel::defaults(bool project) const { return text(application::defaultSideBars({.project = project})); }

QString SideBarModel::migrated(bool project, const QStringList &rightOfGrid) const
{
    std::vector<std::string> panels;
    for (const QString &panel : rightOfGrid)
        panels.push_back(panel.toStdString());
    if (firstStartEmpty)
        return text(SideBars{.first = {.hidden = true}, .second = {.hidden = true}});
    return text(application::migratedSideBars({.project = project}, panels));
}

QVariantMap SideBarModel::read(const QString &json) const
{
    const SideBars bars = parsed(json);
    return {{QStringLiteral("first"), barMap(bars.first)}, {QStringLiteral("second"), barMap(bars.second)}};
}

bool SideBarModel::mayHold(const QString &panel) const { return application::sideBarMayHold(panel.toStdString()); }

QString SideBarModel::moved(const QString &json, const QString &panel, bool second, int index) const
{
    return text(application::moved(parsed(json), panel.toStdString(), id(second), static_cast<std::size_t>(std::max(0, index))));
}

QString SideBarModel::removed(const QString &json, const QString &panel) const
{
    return text(application::removed(parsed(json), panel.toStdString()));
}

QString SideBarModel::shown(const QString &json, const QString &panel) const
{
    return text(application::shownPanel(parsed(json), panel.toStdString()));
}

QString SideBarModel::iconClicked(const QString &json, const QString &panel) const
{
    return text(application::iconClicked(parsed(json), panel.toStdString()));
}

QString SideBarModel::toggled(const QString &json, bool second) const
{
    return text(application::toggled(parsed(json), id(second)));
}

QString SideBarModel::setHidden(const QString &json, bool second, bool hidden) const
{
    SideBars bars = parsed(json);
    if ((second ? bars.second : bars.first).hidden != hidden)
        bars = application::toggled(bars, id(second));
    return text(bars);
}

QString SideBarModel::widened(const QString &json, bool second, int width) const
{
    return text(application::widened(parsed(json), id(second), width));
}

bool SideBarModel::firstOnLeft(const QString &edge, bool rightToLeft) const
{
    return application::sideBarOnLeft(application::aiSideBarEdge(edge.toStdString()), rightToLeft) == SideBarId::First;
}

QString SideBarModel::pluginSideBar(const QString &area, const QString &edge, bool rightToLeft) const
{
    const auto side = application::pluginSideBar(area.toStdString(), application::aiSideBarEdge(edge.toStdString()), rightToLeft);
    if (!side)
        return {};
    return *side == SideBarId::First ? QStringLiteral("first") : QStringLiteral("second");
}

}
