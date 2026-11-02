#pragma once

// Drives an Api from a .jsonl script, one step per line:
//   {"call": "<operation>", "params": {...}, "as": "ai" | "plugin",
//    "result": <subset> | "exact": <value> | "error": "<code>", "errorData": <subset>,
//    "save": {"<name>": "<JSON pointer into the result>"}}
// or {"events": [{"event": "<name>", "data": <subset>}...], "as": ...}, which
// checks and clears the events that caller heard since the last such step.
// A params string "$<name>" is replaced by the value saved under that name.
// Every result is checked against its operation's result schema.

#include "hikari/api/api.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace hikari::api::testing {

// Where `expected` is not contained in `actual`: objects match by the keys
// expected names, arrays item by item at equal length, other values by equality.
std::optional<std::string> mismatch(const Json &expected, const Json &actual, const std::string &path = "");

// Null when the pointer leads nowhere.
const Json *pointer(const Json &value, std::string_view at);

class ScriptClient {
public:
    explicit ScriptClient(Api &api);
    ~ScriptClient();

    // Each failure is a gtest failure naming the script line; the count of steps run.
    int run(const std::filesystem::path &script);

    // The answer to one call, which must come before call() returns.
    Result call(const Caller &caller, std::string_view operation, const Json &params);

    std::map<std::string, Json> saved;

private:
    Json substitute(const Json &value) const;
    void step(const Json &line, const std::string &where);
    void checkEvents(const Json &line, const std::string &where);

    Api &m_api;
    Json m_heard[2];
    std::uint64_t m_subscriptions[2] = {};
};

Caller aiCaller();
Caller pluginCaller();

}
