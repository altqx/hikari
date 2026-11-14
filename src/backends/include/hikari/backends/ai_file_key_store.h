#pragma once

#include "hikari/application/ai_providers.h"

#include <QString>

#include <memory>

namespace hikari::backends::ai {

/// Windows: keys in `folder`, one file per entry, each encrypted for the user with DPAPI. The folder is kept for the
/// user alone and closed to Low-integrity processes; where it can't be, nothing is written (`NotPrivate`). A key
/// missing here is read once from `earlier`, moved here and removed there. Without a folder every call is
/// Unavailable.
class FileKeyStore final : public hikari::application::ai::KeyStore {
public:
    explicit FileKeyStore(QString folder, std::unique_ptr<hikari::application::ai::KeyStore> earlier = nullptr);

    void read(const std::string &providerId, std::function<void(hikari::application::ai::KeyResult)> done) override;
    void write(const std::string &providerId, const std::string &key,
               std::function<void(std::optional<hikari::application::ai::KeyError>)> done) override;
    void remove(const std::string &providerId,
                std::function<void(std::optional<hikari::application::ai::KeyError>)> done) override;

private:
    QString path(const std::string &providerId) const;
    std::optional<hikari::application::ai::KeyError> save(const std::string &providerId, const std::string &key);

    QString m_folder;
    std::unique_ptr<hikari::application::ai::KeyStore> m_earlier;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

}
