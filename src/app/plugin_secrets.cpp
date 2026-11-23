#include "hikari/app/plugin_secrets.h"

#include "hikari/backends/ai_file_key_store.h"
#include "hikari/backends/ai_keychain.h"

#include <QCoreApplication>
#include <QPointer>

namespace hikari::app {

namespace ai = application::ai;

std::unique_ptr<ai::KeyStore> systemKeyStore(const QString &userFolder)
{
    auto keychain = std::make_unique<backends::ai::KeychainKeyStore>(QStringLiteral("HikariSub"));
#ifdef Q_OS_WIN
    return std::make_unique<backends::ai::FileKeyStore>(
        userFolder.isEmpty() ? QString() : userFolder + QStringLiteral("/ai/secrets"), std::move(keychain));
#else
    Q_UNUSED(userFolder);
    return keychain;
#endif
}

PluginSecrets::PluginSecrets(std::unique_ptr<ai::KeyStore> keychain, QObject *parent)
    : QObject(parent), m_keychain(std::move(keychain))
{
}

std::string PluginSecrets::keychainName(std::string_view plugin, std::string_view setting)
{
    return "plugin/" + std::string(plugin) + "/" + std::string(setting);
}

void PluginSecrets::load(const plugins::Manifest &manifest)
{
    for (const plugins::Setting &setting : manifest.settings) {
        Key key{manifest.id, setting.id};
        if (!setting.secret() || !m_read.insert(key).second)
            continue;
        m_keychain->read(keychainName(key.first, key.second), [self = QPointer(this), key](ai::KeyResult result) {
            if (!self)
                return;
            if (const auto *text = std::get_if<std::string>(&result)) {
                self->m_texts[key] = *text;
                emit self->changed(QString::fromStdString(key.first), QString::fromStdString(key.second));
            } else if (std::get<ai::KeyError>(result) == ai::KeyError::Unavailable) {
                self->m_unavailable = true;
            }
        });
    }
}

api::Json PluginSecrets::fill(const plugins::Manifest &manifest, api::Json values) const
{
    for (const plugins::Setting &setting : manifest.settings)
        if (setting.secret()) {
            const auto found = m_texts.find({manifest.id, setting.id});
            values[setting.id] = found == m_texts.end() ? api::Json() : api::Json(found->second);
        }
    return values;
}

std::optional<std::string> PluginSecrets::text(const std::string &plugin, const std::string &setting) const
{
    const auto found = m_texts.find({plugin, setting});
    return found == m_texts.end() ? std::nullopt : std::optional(found->second);
}

QString PluginSecrets::set(const std::string &plugin, const std::string &setting, const api::Json &value)
{
    if (!value.isNull() && !value.isString())
        return QCoreApplication::translate("HikariSub.Plugins", "“%1” takes text").arg(QString::fromStdString(setting));
    if (m_unavailable)
        return QCoreApplication::translate("HikariSub.Plugins", "No system keychain to keep it in");
    const Key key{plugin, setting};
    m_read.insert(key);
    const std::optional<std::string> before = text(plugin, setting);
    const bool clearing = value.isNull() || value.asString().empty();
    if (clearing)
        m_texts.erase(key);
    else
        m_texts[key] = value.asString();
    auto done = [self = QPointer(this), key, before](std::optional<ai::KeyError> error) {
        if (!self || !error || *error == ai::KeyError::NotFound)
            return;
        if (*error == ai::KeyError::Unavailable)
            self->m_unavailable = true;
        if (before)
            self->m_texts[key] = *before;
        else
            self->m_texts.erase(key);
        const QString plugin = QString::fromStdString(key.first), setting = QString::fromStdString(key.second);
        emit self->changed(plugin, setting);
        emit self->notKept(plugin, setting,
                           *error == ai::KeyError::Unavailable
                               ? QCoreApplication::translate("HikariSub.Plugins", "No system keychain to keep it in")
                               : QCoreApplication::translate("HikariSub.Plugins", "The system keychain didn’t keep “%1”").arg(setting));
    };
    const std::string name = keychainName(plugin, setting);
    if (clearing)
        m_keychain->remove(name, std::move(done));
    else
        m_keychain->write(name, value.asString(), std::move(done));
    emit changed(QString::fromStdString(plugin), QString::fromStdString(setting));
    return {};
}

void PluginSecrets::forget(const plugins::Manifest &manifest)
{
    for (const plugins::Setting &setting : manifest.settings)
        if (setting.secret()) {
            m_texts.erase({manifest.id, setting.id});
            m_read.erase({manifest.id, setting.id});
            m_keychain->remove(keychainName(manifest.id, setting.id), [](std::optional<ai::KeyError>) {});
        }
}

}
