#pragma once

#include "hikari/application/tiers.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>

#include <optional>
#include <vector>

namespace hikari::ui {

struct NamedPreset {
    QString name;
    QJsonObject values;
};

// `[{"name", "values"}]`, the stored form of every preset list; nameless entries are dropped.
QList<NamedPreset> presetsOf(const QJsonArray &stored);
QJsonArray jsonOf(const QList<NamedPreset> &presets);

// A named preset list with a Project tier: the Project's preset hides the user's of the same name.
class TieredPresets {
public:
    QList<NamedPreset> &of(application::Tier tier) { return tier == application::Tier::Project ? m_project : m_mine; }
    const QList<NamedPreset> &of(application::Tier tier) const
    {
        return tier == application::Tier::Project ? m_project : m_mine;
    }

    // The Project's names, then the user's; without a Project the user's alone.
    std::vector<application::TieredName> rows() const;
    // [{name, tier ("project", "mine"), hidden}], as rows().
    QVariantList rowMaps() const;
    // The tier holding `name`, the Project's first; `tier` asks for that one only.
    std::optional<application::Tier> find(const QString &name, std::optional<application::Tier> tier = {}) const;
    const NamedPreset *preset(const QString &name, std::optional<application::Tier> tier = {}) const;
    // A user preset the Project's preset of the same name hides.
    bool hidden(const QString &name, application::Tier tier) const;

    // Replaces a preset of the same name in `tier`, else appends.
    void save(application::Tier tier, const NamedPreset &preset);
    // Refused when `to` is taken in the same tier.
    bool rename(application::Tier tier, const QString &from, const QString &to);
    bool remove(application::Tier tier, const QString &name);
    // The name a clash suggests, when the other tier already has `name`.
    std::optional<QString> clash(const QString &name, application::Tier from) const;
    // Copies, or moves, `name` to the other tier. Returns the name it landed under; none when skipped.
    std::optional<QString> send(const QString &name, application::Tier from, const application::ClashChoice &choice,
                                bool move);

private:
    QList<NamedPreset> m_project, m_mine;
};

application::Tier otherTierOf(application::Tier tier);
QString tierText(application::Tier tier);
std::optional<application::Tier> tierFromText(const QString &text);
// QML's [{name, action ("replace", "rename", "skip"), newName}]: the choice for `name`, Skip without one.
application::ClashChoice clashChoiceFor(const QString &name, const QVariantList &choices);

}
