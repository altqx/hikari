#pragma once

#include "hikari/api/host.h"
#include "hikari/api/json.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::api {

// Grows by one with each release that adds to the API; 0 until the first.
inline constexpr int kApiVersion = 0;

enum class Access { Read, Write };
std::string_view accessName(Access access);

namespace errors {
inline constexpr std::string_view kUnknownOperation = "unknown_operation";
inline constexpr std::string_view kInvalidParams = "invalid_params";
inline constexpr std::string_view kNotFound = "not_found";
inline constexpr std::string_view kNotGranted = "not_granted";
inline constexpr std::string_view kUnavailable = "unavailable";
}

struct Error {
    std::string code;
    std::string message;
    Json data;
};
using Result = std::expected<Json, Error>;
using Reply = std::function<void(Result)>;
std::unexpected<Error> fail(std::string_view code, std::string message, Json data = {});

struct Caller {
    std::string id;
    std::string label; // names the caller's undo steps
    // Without it only the editing target is visible.
    bool allDocuments = false;
};

struct Context {
    const Host &host;
    const Caller &caller;
};

// A handler replies exactly once, now or later, on the GUI thread.
using Handler = std::function<void(const Context &, const Json &params, Reply)>;

struct Operation {
    std::string name;
    Access access = Access::Read;
    std::string summary;
    Json params;
    Json result;
    int since = 1;
    std::optional<int> deprecatedIn;
    Handler handler;
};

class Registry {
public:
    // Names are unique; a second add of a name is ignored and returns false.
    bool add(Operation operation);
    const Operation *find(std::string_view name) const;
    const std::vector<Operation> &operations() const { return m_operations; }
    // What "$ref": "#/$defs/<name>" resolves into, for every schema here.
    const Json &defs() const { return m_defs; }
    void define(std::string_view name, Json schema) { m_defs[name] = std::move(schema); }

private:
    std::vector<Operation> m_operations;
    Json m_defs = Json::object();
};

class Api {
public:
    explicit Api(Host host) : m_host(host) {}

    // Every operation the API offers.
    static const Registry &registry();

    // Params are validated against the operation's schema before it runs;
    // null counts as {}.
    void call(const Caller &caller, std::string_view operation, const Json &params, Reply reply);

private:
    Host m_host;
};

}
