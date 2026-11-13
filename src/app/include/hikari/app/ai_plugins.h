#pragma once

#include "hikari/api/host.h"

#include <QString>

#include <functional>

namespace hikari::app {

class PluginManager;
class PluginRuntime;

// The plugins agents read, run and install, and the AI plugins folder they scaffold into. It installs only
// through PluginManager, the path Install from file… takes.
class AiPlugins final : public api::PluginHost {
public:
    struct Wiring {
        QString folder;    // the AI plugins folder; empty: no scaffolds
        QString installed; // the installed plugins' folder
        std::function<PluginManager *()> manager;
        std::function<PluginRuntime *()> runtime;
    };
    explicit AiPlugins(Wiring wiring);

    std::vector<api::PluginState> plugins() const override;
    std::optional<std::string> manifest(std::string_view plugin) const override;
    api::PluginRun run(std::string_view plugin, std::string_view command) override;
    std::expected<api::ScaffoldedPlugin, std::string> scaffold(std::string_view plugin,
                                                               const std::vector<api::PluginFile> &files) override;
    std::optional<std::string> scaffoldedPackage(std::string_view plugin) const override;
    std::expected<api::PluginReview, std::string> review(const std::string &package) override;
    std::expected<api::PluginReview, std::string> install(const std::string &package, const std::string &digest) override;

private:
    Wiring m_wiring;
};

}
