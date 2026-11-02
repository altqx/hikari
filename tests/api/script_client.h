#pragma once

// Drives an Api from a .jsonl script, one step per line:
//   {"call": "<operation>", "params": {...}, "as": "ai" | "plugin",
//    "result": <subset> | "exact": <value> | "error": "<code>", "errorData": <subset>,
//    "save": {"<name>": "<JSON pointer into the result>"}}
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
    explicit ScriptClient(Api &api) : m_api(api) {}

    // Each failure is a gtest failure naming the script line; the count of steps run.
    int run(const std::filesystem::path &script);

    // The answer to one call, which must come before call() returns.
    Result call(const Caller &caller, std::string_view operation, const Json &params);

    std::map<std::string, Json> saved;

private:
    Json substitute(const Json &value) const;
    void step(const Json &line, const std::string &where);

    Api &m_api;
};

Caller aiCaller();
Caller pluginCaller();

}
