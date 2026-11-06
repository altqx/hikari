#pragma once

// Runs Lua nodes in a hikari-lua-helper of its own, driven from a thread of its own, so run() may
// block on any other thread. A batch past the time limit, or a lost helper, ends the helper and
// stops the run; the next batch starts a new helper.

#include "hikari/application/node_effects.h"
#include "hikari/backends/sandbox.h"

#include <QString>

#include <memory>
#include <optional>

namespace hikari::backends {

// HikariSub.Lua.Nodes: the helper's folder to read and execute, nothing else, and no network.
sandbox::Profile luaNodeProfile(const QString &helperPath);

class LuaNodeHelper : public application::LuaNodePort {
public:
    struct Options {
        QString helper;
        int timeLimitMs = 10'000;
        // Where it can't be enforced, Lua nodes still run and hikari.nodes.sandbox says what is missing.
        std::optional<sandbox::Profile> sandbox;
    };

    explicit LuaNodeHelper(Options options);
    ~LuaNodeHelper() override;
    LuaNodeHelper(const LuaNodeHelper &) = delete;
    LuaNodeHelper &operator=(const LuaNodeHelper &) = delete;

    std::expected<std::vector<std::vector<application::NodeValue>>, Failure> run(const Call &call) override;

    const sandbox::Coverage &sandboxCoverage() const;
    // Helpers started so far.
    int starts() const;

private:
    struct Worker;
    std::unique_ptr<Worker> m;
};

}
