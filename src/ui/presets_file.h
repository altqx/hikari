#pragma once

#include <cstdint>

#include <toml++/toml.hpp>

#include <QString>
#include <QStringList>

#include <string_view>

namespace hikari::ui {

// A Project's presets tier: .hikari/presets.toml, holding automation.dialogPresets, shiftTimes.presets and
// postprocessor.presets as settings.toml holds them. Made on the first write.
class PresetsFile {
public:
    static constexpr std::int64_t kVersion = 1;

    // An empty root is no Project: every list is empty and nothing is written.
    explicit PresetsFile(QString root = {});

    const QString &root() const { return m_root; }
    QString path() const;
    // The rows SettingsStore::list gives for the same setting.
    QStringList list(std::string_view id) const;
    // False when there is no Project or the file can't be written.
    bool set(std::string_view id, const QStringList &rows);

private:
    QString m_root;
    toml::table m_table;
    bool m_writable = true;
};

}
