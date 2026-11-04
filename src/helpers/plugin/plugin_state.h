#pragma once

#include "hikari/api/json.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace hikari::plugin_helper {

enum class Tier { Restricted, Unrestricted };
enum class Level { Info, Warning, Error };

// What a plugin asks of HikariSub. Each call blocks until HikariSub answers.
class Host {
public:
    virtual ~Host() = default;
    // The error is the API's {code, message, data}.
    virtual std::expected<api::Json, api::Json> call(const std::string &operation, const api::Json &params) = 0;
    virtual void notify(Level level, bool toast, const std::string &text) = 0;
    virtual std::expected<void, std::string> setSetting(const std::string &id, const api::Json &value) = 0;
    virtual std::expected<void, std::string> subscribe(const std::string &event) = 0;
    // Waits for the app-wide edit lock; the error says why it wasn't given.
    virtual std::expected<void, std::string> lock() = 0;
    virtual void unlock() = 0;
    // The plugin's panels and dialogs; answered like call.
    virtual std::expected<api::Json, api::Json> ui(const std::string &operation, const api::Json &params) = 0;
};

struct Options {
    std::filesystem::path root;
    std::string main;
    std::string id;
    std::string version;
    Tier tier = Tier::Restricted;
    std::size_t memoryLimit = 256u << 20;
    std::string locale;
    api::Json settings = api::Json::object();
    std::vector<std::string> commands;
};

// One plugin's LuaJIT state and its `hikari` global.
class PluginState {
public:
    // Creates the state and runs the main file; the error is the text to show.
    static std::expected<std::unique_ptr<PluginState>, std::string> load(Options options, Host &host);
    ~PluginState();
    PluginState(const PluginState &) = delete;
    PluginState &operator=(const PluginState &) = delete;

    std::vector<std::string> handledCommands() const;
    std::expected<void, std::string> run(const std::string &command, Host &host);
    // Runs every handler for `event`; the error is the first handler's.
    std::expected<void, std::string> dispatch(const std::string &event, const api::Json &data, Host &host);
    std::size_t memoryInUse() const { return m_budget.used; }

    struct Budget {
        std::size_t used = 0;
        std::size_t limit = 0;
    };
    // For the Lua bindings.
    struct Access;

private:
    explicit PluginState(Options options);
    std::expected<void, std::string> open();
    std::expected<void, std::string> runPrelude();
    std::expected<void, std::string> protectedCall(int arguments, Host &host);
    std::expected<void, std::string> readCatalog();

    Options m_options;
    Budget m_budget;
    lua_State *m_lua = nullptr;
    Host *m_host = nullptr;
    api::Json m_catalog = api::Json::object();
};

}
