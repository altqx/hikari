#include "export_recipes.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>

namespace hikari::ui {

namespace {

using application::DialogValue;
using application::ExportRecipe;
using application::ExportStep;
using application::Tier;

constexpr const char *kRecipes = "export.recipes";

DialogValue valueOf(const QJsonValue &v)
{
    if (v.isBool())
        return v.toBool();
    if (v.isString())
        return v.toString().toStdString();
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (std::trunc(d) == d && std::abs(d) <= 2147483647.0)
            return static_cast<int>(d);
        return d;
    }
    return std::monostate{};
}

QJsonValue jsonValue(const DialogValue &v)
{
    if (const auto *s = std::get_if<std::string>(&v))
        return QString::fromStdString(*s);
    if (const auto *i = std::get_if<int>(&v))
        return *i;
    if (const auto *d = std::get_if<double>(&v))
        return *d;
    if (const auto *b = std::get_if<bool>(&v))
        return *b;
    return QJsonValue::Null;
}

}

ExportRecipes::ExportRecipes(SettingsStore &store) : m_store(&store)
{
    m_presets.of(Tier::Mine) = presetsOfRows(m_store->list(kRecipes));
}

void ExportRecipes::setProjectRoot(const QString &root)
{
    if (root == m_projectFile.root())
        return;
    m_projectFile = PresetsFile(root);
    m_presets.of(Tier::Project) = presetsOfRows(m_projectFile.list(kRecipes));
}

std::optional<ExportRecipe> ExportRecipes::recipe(const QString &name, std::optional<Tier> tier) const
{
    const NamedPreset *preset = m_presets.preset(name, tier);
    if (!preset)
        return std::nullopt;
    return recipeOf(preset->values);
}

bool ExportRecipes::save(const QString &name, Tier tier, const ExportRecipe &recipe)
{
    if (name.trimmed().isEmpty())
        return false;
    const Tier to = hasProject() ? tier : Tier::Mine;
    m_presets.save(to, {name.trimmed(), jsonOf(recipe)});
    store(to);
    return true;
}

bool ExportRecipes::remove(const QString &name, Tier tier)
{
    if (!m_presets.remove(tier, name))
        return false;
    store(tier);
    return true;
}

bool ExportRecipes::rename(Tier tier, const QString &from, const QString &to)
{
    if (to.trimmed().isEmpty() || !m_presets.rename(tier, from, to.trimmed()))
        return false;
    store(tier);
    return true;
}

void ExportRecipes::store(Tier tier)
{
    const QStringList rows = rowsOf(m_presets.of(tier));
    if (tier == Tier::Project)
        m_projectFile.set(kRecipes, rows);
    else
        m_store->set(kRecipes, rows);
}

QJsonObject ExportRecipes::jsonOf(const ExportRecipe &recipe)
{
    QJsonArray steps;
    for (const ExportStep &step : recipe.steps) {
        QJsonObject settings;
        for (const auto &[name, value] : step.settings)
            settings.insert(QString::fromStdString(name), jsonValue(value));
        steps.append(QJsonObject{{QStringLiteral("id"), QString::fromStdString(step.id)},
                                 {QStringLiteral("on"), step.on},
                                 {QStringLiteral("settings"), settings}});
    }
    return {{QStringLiteral("steps"), steps}};
}

ExportRecipe ExportRecipes::recipeOf(const QJsonObject &json)
{
    ExportRecipe recipe;
    for (const QJsonValue &v : json.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        ExportStep step{id.toStdString(), o.value(QStringLiteral("on")).toBool(true), {}};
        const QJsonObject settings = o.value(QStringLiteral("settings")).toObject();
        for (auto it = settings.begin(); it != settings.end(); ++it)
            step.settings.emplace(it.key().toStdString(), valueOf(it.value()));
        recipe.steps.push_back(std::move(step));
    }
    return recipe;
}

ExportRecipe ExportRecipes::recipeOf(const QVariantList &steps)
{
    return recipeOf(QJsonObject{{QStringLiteral("steps"), QJsonArray::fromVariantList(steps)}});
}

QVariantList ExportRecipes::stepsOf(const ExportRecipe &recipe)
{
    return jsonOf(recipe).value(QStringLiteral("steps")).toArray().toVariantList();
}

}
