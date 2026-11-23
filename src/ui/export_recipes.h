#pragma once

#include "hikari/application/export_steps.h"
#include "presets_file.h"
#include "settings_store.h"
#include "tiered_presets.h"

#include <QJsonObject>
#include <QString>
#include <QVariantList>

#include <optional>

namespace hikari::ui {

// Export as's recipes: presets with ADR 0030's tiers, kept in export.recipes.
class ExportRecipes {
public:
    explicit ExportRecipes(SettingsStore &store);

    void setProjectRoot(const QString &root);
    bool hasProject() const { return !m_projectFile.root().isEmpty(); }

    // [{name, tier ("project", "mine"), hidden}]: the Project's, then yours.
    QVariantList rows() const { return m_presets.rowMaps(); }
    // The recipe of that name, the Project's first; `tier` asks for that one only.
    std::optional<application::ExportRecipe> recipe(const QString &name,
                                                    std::optional<application::Tier> tier = {}) const;
    // Replaces the tier's recipe of the same name, else adds it. Without a Project it goes to yours.
    bool save(const QString &name, application::Tier tier, const application::ExportRecipe &recipe);
    bool remove(const QString &name, application::Tier tier);
    bool rename(application::Tier tier, const QString &from, const QString &to);

    // {steps: [{id, on, settings: {name: value}}]}
    static QJsonObject jsonOf(const application::ExportRecipe &recipe);
    static application::ExportRecipe recipeOf(const QJsonObject &json);
    // QML's [{id, on, settings}].
    static application::ExportRecipe recipeOf(const QVariantList &steps);
    static QVariantList stepsOf(const application::ExportRecipe &recipe);

private:
    void store(application::Tier tier);

    SettingsStore *m_store;
    PresetsFile m_projectFile;
    TieredPresets m_presets;
};

}
