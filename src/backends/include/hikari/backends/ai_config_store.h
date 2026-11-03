#pragma once

#include "hikari/application/ai_providers.h"

#include <QString>

namespace hikari::backends::ai {

/// Keeps the user's providers and role models in a JSON file. Keys are never part of it.
class JsonConfigStore final : public application::ai::ConfigStore {
public:
    explicit JsonConfigStore(QString path) : m_path(std::move(path)) {}
    application::ai::Config load() override;
    bool save(const application::ai::Config &config) override;

private:
    QString m_path;
};

}
