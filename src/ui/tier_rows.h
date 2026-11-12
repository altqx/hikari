#pragma once

#include "hikari/application/clash_choice.h"
#include "hikari/application/tiers.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <string>
#include <vector>

namespace hikari::ui {

inline QString tierName(application::Tier tier)
{
    return tier == application::Tier::Project ? QStringLiteral("project") : QStringLiteral("mine");
}

inline application::Tier tierNamed(const QString &name, bool hasProject)
{
    return name == QLatin1String("project") && hasProject ? application::Tier::Project : application::Tier::Mine;
}

struct TierClash {
    application::ClashChoice choice;
    std::u16string newName;
};

// The last of ClashReview's [{action, newName}]; Skip when there are none.
inline TierClash tierClashOf(const QVariantList &choices)
{
    TierClash out;
    for (const QVariant &c : choices) {
        const QVariantMap choice = c.toMap();
        const QString action = choice.value(QStringLiteral("action")).toString();
        out.choice.action = action == QLatin1String("replace")  ? application::ClashAction::Replace
                            : action == QLatin1String("rename") ? application::ClashAction::Rename
                                                                : application::ClashAction::Skip;
        out.newName = choice.value(QStringLiteral("newName")).toString().toStdU16String();
    }
    return out;
}

// ClashReview's row for `current`, whose name the other tier has, with a free name as the suggestion.
template <class Item, class Same>
QVariantList tierClashRows(const std::vector<Item> &all, const Item &current, Same same)
{
    std::vector<std::u16string> taken;
    for (const Item &item : application::itemsIn(all, application::otherTier(current.mark.tier)))
        taken.push_back(item.name);
    const auto free = application::landingNameBy(taken, current.name, {application::ClashAction::Rename, {}},
                                                 std::u16string(), same);
    return {QVariantMap{{QStringLiteral("name"), QString::fromStdU16String(current.name)},
                        {QStringLiteral("suggestion"), QString::fromStdU16String(free.value_or(current.name))}}};
}

}
