#pragma once

#include "hikari/application/dialog_memory.h"

#include "presets_file.h"
#include "settings_store.h"
#include "tiered_presets.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>

namespace hikari::ui {

class ScriptDialogMemory {
public:
    static constexpr int kKeptAnswers = 200;

    explicit ScriptDialogMemory(SettingsStore *settings = nullptr) : m_settings(settings) {}
    void setSettings(SettingsStore *settings) { m_settings = settings; }

    static QString keyOf(const QString &script, const application::DialogRequest &request);

    bool fillLastAnswers() const;
    std::optional<application::LastDialogAnswer> lastAnswer(const QString &key) const;
    void remember(const QString &key, const application::LastDialogAnswer &answer);

    // The Project folder whose .hikari/presets.toml holds the Project's presets; empty for none.
    void setProjectRoot(const QString &root);
    bool hasProject() const { return !m_project.root().isEmpty(); }

    // A dialog's presets, the Project's hiding the user's of the same name. Without a tier the
    // Project's preset is found first. A hidden user preset can't be deleted.
    QStringList presets(const QString &key) const;
    // [{name, tier ("project", "mine"), hidden}].
    QVariantList presetRows(const QString &key) const;
    std::optional<application::NamedDialogValues> preset(const QString &key, const QString &name,
                                                         std::optional<application::Tier> tier = {}) const;
    void savePreset(const QString &key, const QString &name, const application::NamedDialogValues &values,
                    application::Tier tier = application::Tier::Mine);
    void deletePreset(const QString &key, const QString &name, std::optional<application::Tier> tier = {});
    // The name a clash suggests when the other tier already has `name`.
    std::optional<QString> presetClash(const QString &key, const QString &name, application::Tier from) const;
    // Copies or moves a preset to the other tier; the name it landed under, none when skipped.
    std::optional<QString> sendPreset(const QString &key, const QString &name, application::Tier from,
                                      const application::ClashChoice &choice, bool move);

private:
    TieredPresets tiered(const QString &key) const;
    void store(const QString &key, const TieredPresets &presets, application::Tier tier);

    QPointer<SettingsStore> m_settings;
    PresetsFile m_project;
};

}
