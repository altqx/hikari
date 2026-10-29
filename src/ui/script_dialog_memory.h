#pragma once

#include "hikari/application/dialog_memory.h"

#include "settings_store.h"

#include <QPointer>
#include <QString>
#include <QStringList>

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

    QStringList presets(const QString &key) const;
    std::optional<application::NamedDialogValues> preset(const QString &key, const QString &name) const;
    void savePreset(const QString &key, const QString &name, const application::NamedDialogValues &values);
    void deletePreset(const QString &key, const QString &name);

private:
    QPointer<SettingsStore> m_settings;
};

}
