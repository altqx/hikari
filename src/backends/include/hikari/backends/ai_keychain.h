#pragma once

#include "hikari/application/ai_providers.h"

#include <QString>

namespace hikari::backends::ai {

/// Provider keys in the OS keychain through QtKeychain: the Secret Service or KWallet on Linux, the Credential
/// Store on Windows. Without one, every call fails with Unavailable; nothing falls back to a file.
class KeychainKeyStore final : public hikari::application::ai::KeyStore {
public:
    explicit KeychainKeyStore(QString service) : m_service(std::move(service)) {}

    void read(const std::string &providerId, std::function<void(hikari::application::ai::KeyResult)> done) override;
    void write(const std::string &providerId, const std::string &key,
               std::function<void(std::optional<hikari::application::ai::KeyError>)> done) override;
    void remove(const std::string &providerId,
                std::function<void(std::optional<hikari::application::ai::KeyError>)> done) override;

private:
    QString m_service;
};

}
