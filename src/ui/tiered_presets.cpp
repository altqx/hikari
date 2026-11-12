#include "tiered_presets.h"

#include <QVariantMap>

#include <algorithm>

namespace hikari::ui {

using application::Tier;

namespace {

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size()));
}

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), qsizetype(s.size()));
}

std::vector<std::u8string> namesOf(const QList<NamedPreset> &presets)
{
    std::vector<std::u8string> out;
    for (const auto &p : presets)
        out.push_back(u8(p.name));
    return out;
}

auto named(const QString &name)
{
    return [&name](const NamedPreset &p) { return p.name == name; };
}

}

QList<NamedPreset> presetsOf(const QJsonArray &stored)
{
    QList<NamedPreset> out;
    for (const auto &entry : stored) {
        const QJsonObject o = entry.toObject();
        const QString name = o.value(QLatin1String("name")).toString();
        if (!name.isEmpty())
            out << NamedPreset{name, o.value(QLatin1String("values")).toObject()};
    }
    return out;
}

QJsonArray jsonOf(const QList<NamedPreset> &presets)
{
    QJsonArray out;
    for (const auto &p : presets)
        out.append(QJsonObject{{QLatin1String("name"), p.name}, {QLatin1String("values"), p.values}});
    return out;
}

std::vector<application::TieredName> TieredPresets::rows() const
{
    return application::hideByName(namesOf(m_project), namesOf(m_mine));
}

QVariantList TieredPresets::rowMaps() const
{
    QVariantList out;
    for (const auto &row : rows())
        out << QVariantMap{{QStringLiteral("name"), qs(row.name)},
                           {QStringLiteral("tier"), tierText(row.tier)},
                           {QStringLiteral("hidden"), row.hidden}};
    return out;
}

std::optional<Tier> TieredPresets::find(const QString &name, std::optional<Tier> tier) const
{
    if (tier)
        return std::ranges::any_of(of(*tier), named(name)) ? tier : std::nullopt;
    return application::resolveByName(namesOf(m_project), namesOf(m_mine), u8(name));
}

const NamedPreset *TieredPresets::preset(const QString &name, std::optional<Tier> tier) const
{
    const auto at = find(name, tier);
    if (!at)
        return nullptr;
    const auto &list = of(*at);
    return &*std::ranges::find_if(list, named(name));
}

bool TieredPresets::hidden(const QString &name, Tier tier) const
{
    return tier == Tier::Mine && std::ranges::any_of(m_project, named(name));
}

void TieredPresets::save(Tier tier, const NamedPreset &preset)
{
    auto &list = of(tier);
    if (const auto it = std::ranges::find_if(list, named(preset.name)); it != list.end())
        *it = preset;
    else
        list << preset;
}

bool TieredPresets::rename(Tier tier, const QString &from, const QString &to)
{
    auto &list = of(tier);
    const auto it = std::ranges::find_if(list, named(from));
    if (it == list.end() || (to != from && std::ranges::any_of(list, named(to))))
        return false;
    it->name = to;
    return true;
}

bool TieredPresets::remove(Tier tier, const QString &name)
{
    return of(tier).removeIf(named(name)) > 0;
}

std::optional<QString> TieredPresets::clash(const QString &name, Tier from) const
{
    const auto &taken = of(application::otherTier(from));
    if (!std::ranges::any_of(taken, named(name)))
        return std::nullopt;
    const auto suggestion = application::landingName(namesOf(taken), u8(name), {application::ClashAction::Rename, {}});
    return qs(suggestion.value_or(u8(name)));
}

std::optional<QString> TieredPresets::send(const QString &name, Tier from, const application::ClashChoice &choice,
                                           bool move)
{
    const NamedPreset *source = preset(name, from);
    if (!source)
        return std::nullopt;
    const Tier to = application::otherTier(from);
    const auto landed = application::landingName(namesOf(of(to)), u8(name), choice);
    if (!landed)
        return std::nullopt;
    const NamedPreset copy{qs(*landed), source->values};
    if (move)
        remove(from, name);
    save(to, copy);
    return copy.name;
}

QString tierText(Tier tier)
{
    return tier == Tier::Project ? QStringLiteral("project") : QStringLiteral("mine");
}

std::optional<Tier> tierFromText(const QString &text)
{
    if (text == QLatin1String("project"))
        return Tier::Project;
    if (text == QLatin1String("mine"))
        return Tier::Mine;
    return std::nullopt;
}

application::ClashChoice clashChoiceFor(const QString &name, const QVariantList &choices)
{
    application::ClashChoice clash;
    for (const auto &c : choices) {
        const auto choice = c.toMap();
        if (choice.value(QStringLiteral("name")).toString() != name)
            continue;
        const QString action = choice.value(QStringLiteral("action")).toString();
        clash.action = action == QLatin1String("replace") ? application::ClashAction::Replace
                       : action == QLatin1String("rename") ? application::ClashAction::Rename
                                                           : application::ClashAction::Skip;
        clash.newName = u8(choice.value(QStringLiteral("newName")).toString().trimmed());
    }
    return clash;
}

std::u16string newNameOf(const application::ClashChoice &choice)
{
    return qs(choice.newName).toStdU16String();
}

}
