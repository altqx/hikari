#pragma once

// AD2 (#311): script dialogs' last answers and presets, kept in the profile
// (docs/automation.md). Both are kept per dialog key: the script's file
// and the dialog's controls (each one's class and name), so the script's
// macros that show the same dialog share them and a script with two
// dialogs keeps them apart.
//
// Last answers are "automation.dialogAnswers": a "<key>\t<JSON>" row per
// dialog, the one answered last first; the 200 keys used longest ago are
// dropped first. Presets are "automation.dialogPresets": a "<key>\t<JSON>"
// row per dialog holding its presets in the order they were saved, kept
// until deleted. Values are kept by control name; labels and buttons are
// not kept.

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

    // "Fill in my last answers" (AD2-fill-last-answers).
    bool fillLastAnswers() const;
    std::optional<application::LastDialogAnswer> lastAnswer(const QString &key) const;
    void remember(const QString &key, const application::LastDialogAnswer &answer);

    QStringList presets(const QString &key) const;
    std::optional<application::NamedDialogValues> preset(const QString &key, const QString &name) const;
    // Saves `values` as `name`, in place of a preset of that name.
    void savePreset(const QString &key, const QString &name, const application::NamedDialogValues &values);
    void deletePreset(const QString &key, const QString &name);

private:
    QPointer<SettingsStore> m_settings;
};

} // namespace hikari::ui
