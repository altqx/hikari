#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"
#include "hikari/plugins/manifest.h"

#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace hikari::app {

// The system keychain HikariSub keeps provider keys and plugin secrets in: QtKeychain, and on Windows DPAPI files
// under <user folder>/ai/secrets.
std::unique_ptr<application::ai::KeyStore> systemKeyStore(const QString &userFolder);

// Plugins' `secret` settings, kept in the keychain as `plugin/<plugin id>/<setting id>` and read into memory once.
class PluginSecrets final : public QObject {
    Q_OBJECT

public:
    explicit PluginSecrets(std::unique_ptr<application::ai::KeyStore> keychain, QObject *parent = nullptr);

    static std::string keychainName(std::string_view plugin, std::string_view setting);

    // Reads the manifest's secrets not read yet; `changed` follows each one found.
    void load(const plugins::Manifest &manifest);
    // The manifest's values with each secret's text, or null while it is unset or unread.
    api::Json fill(const plugins::Manifest &manifest, api::Json values) const;
    std::optional<std::string> text(const std::string &plugin, const std::string &setting) const;
    // Text keeps it, null removes it. Why it was refused, or empty once it is on its way to the keychain.
    QString set(const std::string &plugin, const std::string &setting, const api::Json &value);
    void forget(const plugins::Manifest &manifest);

signals:
    void changed(const QString &plugin, const QString &setting);
    // The keychain refused a value set earlier; the old one stays.
    void notKept(const QString &plugin, const QString &setting, const QString &problem);

private:
    using Key = std::pair<std::string, std::string>;

    std::unique_ptr<application::ai::KeyStore> m_keychain;
    std::map<Key, std::string> m_texts;
    std::set<Key> m_read;
    bool m_unavailable = false;
};

}
