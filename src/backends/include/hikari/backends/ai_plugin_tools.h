#pragma once

#include "hikari/api/api.h"
#include "hikari/api/json.h"

#include <QObject>
#include <QPointer>

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends::ai {

class AiSidecarHost;

inline constexpr const char *kPluginToolsSet = "_hikari/pluginTools.set";
inline constexpr const char *kPluginToolsCall = "_hikari/pluginTools.call";
/// The operation a plugin tool call goes through AgentThreads' gate as; its params are pluginToolCallParams'.
inline constexpr std::string_view kPluginToolOperation = "plugin.tool";

/// One `contributes.aiTools` entry of a plugin that runs.
struct PluginToolEntry {
    std::string name;
    std::string plugin; // the runtime's key for the plugin
    std::string pluginName;
    std::string command;
    std::string description;
    api::Json parameters;
    api::Json result; // null without one
    bool read = false;
    /// The user trusts the plugin's tool marks.
    bool trusted = false;

    bool readOnly() const { return read && trusted; }
    friend bool operator==(const PluginToolEntry &, const PluginToolEntry &) = default;
};

/// `p_<the first 8 hex digits of the SHA-256 of "<plugin>/<command>">_<command>`, with '-' as '_', cut to 64.
std::string pluginToolName(const std::string &plugin, const std::string &command);

/// What the gate sees of a call: whose tool, its arguments, and whether it only reads after trust.
api::Json pluginToolCallParams(const PluginToolEntry &tool, const api::Json &arguments);

/// The plugins' AI tools, for one running sidecar: it lists them, checks each call's arguments, gates it for the
/// session, then runs it as the plugin and checks the answer.
class PluginTools final : public QObject {
    Q_OBJECT
public:
    using Answer = std::function<void(std::expected<api::Json, api::Error>)>;
    using Run = std::function<void(const PluginToolEntry &tool, const api::Json &arguments, Answer answer)>;

    explicit PluginTools(QObject *parent = nullptr);
    ~PluginTools() override;

    void setTools(std::vector<PluginToolEntry> tools);
    const std::vector<PluginToolEntry> &tools() const { return m_tools; }
    void setRun(Run run) { m_run = std::move(run); }
    void attach(AiSidecarHost &host);

private:
    void announce();
    const PluginToolEntry *find(const std::string &name) const;

    std::vector<PluginToolEntry> m_tools;
    Run m_run;
    QPointer<AiSidecarHost> m_host;
    bool m_sidecarHasTools = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
