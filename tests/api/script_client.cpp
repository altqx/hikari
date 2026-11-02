#include "script_client.h"

#include "hikari/api/schema.h"

#include <gtest/gtest.h>

#include <fstream>

namespace hikari::api::testing {

Caller aiCaller()
{
    return Caller{"test.ai", "Test AI", true};
}

Caller pluginCaller()
{
    return Caller{"plugin.test", "Test plugin", false};
}

std::optional<std::string> mismatch(const Json &expected, const Json &actual, const std::string &path)
{
    if (expected.isObject() && actual.isObject()) {
        for (const Json::Member &m : expected.asObject()) {
            const Json *found = actual.find(m.key);
            if (!found)
                return path + "/" + m.key + ": missing";
            if (auto problem = mismatch(m.value, *found, path + "/" + m.key))
                return problem;
        }
        return std::nullopt;
    }
    if (expected.isArray() && actual.isArray()) {
        const auto &e = expected.asArray();
        const auto &a = actual.asArray();
        if (e.size() != a.size())
            return path + ": " + std::to_string(a.size()) + " items, expected " + std::to_string(e.size());
        for (std::size_t i = 0; i < e.size(); ++i)
            if (auto problem = mismatch(e[i], a[i], path + "/" + std::to_string(i)))
                return problem;
        return std::nullopt;
    }
    if (!(expected == actual))
        return path + ": " + actual.dump() + ", expected " + expected.dump();
    return std::nullopt;
}

const Json *pointer(const Json &value, std::string_view at)
{
    const Json *current = &value;
    while (!at.empty()) {
        if (at.front() != '/')
            return nullptr;
        at.remove_prefix(1);
        const std::size_t end = at.find('/');
        const std::string token(at.substr(0, end));
        at = end == std::string_view::npos ? std::string_view() : at.substr(end);
        if (current->isObject()) {
            current = current->find(token);
        } else if (current->isArray()) {
            const std::size_t index = std::stoul(token);
            current = index < current->asArray().size() ? &current->asArray()[index] : nullptr;
        } else {
            return nullptr;
        }
        if (!current)
            return nullptr;
    }
    return current;
}

Result ScriptClient::call(const Caller &caller, std::string_view operation, const Json &params)
{
    std::optional<Result> answer;
    m_api.call(caller, operation, params, [&](Result r) {
        EXPECT_FALSE(answer) << operation << " replied twice";
        answer = std::move(r);
    });
    if (!answer)
        return fail("test_no_reply", std::string(operation) + " did not reply before returning");
    if (*answer) {
        const Registry &registry = Api::registry();
        if (const Operation *op = registry.find(operation))
            if (auto failure = validate(op->result, **answer, registry.defs()))
                ADD_FAILURE() << operation << " result breaks its schema at " << failure->path << ": "
                              << failure->message << "\n"
                              << (*answer)->dump(2);
    }
    return *answer;
}

Json ScriptClient::substitute(const Json &value) const
{
    if (value.isString() && value.asString().starts_with("$")) {
        const auto it = saved.find(value.asString().substr(1));
        return it == saved.end() ? value : it->second;
    }
    if (value.isArray()) {
        Json out = Json::array();
        for (const Json &item : value.asArray())
            out.push(substitute(item));
        return out;
    }
    if (value.isObject()) {
        Json out = Json::object();
        for (const Json::Member &m : value.asObject())
            out[m.key] = substitute(m.value);
        return out;
    }
    return value;
}

void ScriptClient::step(const Json &line, const std::string &where)
{
    const Json *operation = line.find("call");
    if (!operation || !operation->isString()) {
        ADD_FAILURE() << where << ": a step needs \"call\"";
        return;
    }
    const Json *as = line.find("as");
    const Caller caller = as && as->asString() == "plugin" ? pluginCaller() : aiCaller();
    const Json params = line.find("params") ? substitute(*line.find("params")) : Json();
    const Result result = call(caller, operation->asString(), params);
    const std::string what = where + " " + operation->asString();
    if (const Json *code = line.find("error")) {
        if (result) {
            ADD_FAILURE() << what << ": expected error " << code->dump() << ", got " << result->dump();
            return;
        }
        EXPECT_EQ(result.error().code, code->asString()) << what << ": " << result.error().message;
        EXPECT_FALSE(result.error().message.empty()) << what;
        if (const Json *data = line.find("errorData"))
            if (auto problem = mismatch(*data, result.error().data))
                ADD_FAILURE() << what << ": error data " << *problem;
        return;
    }
    if (!result) {
        ADD_FAILURE() << what << ": " << result.error().code << ": " << result.error().message;
        return;
    }
    if (const Json *expected = line.find("result"))
        if (auto problem = mismatch(substitute(*expected), *result))
            ADD_FAILURE() << what << ": " << *problem << "\n" << result->dump(2);
    if (const Json *exact = line.find("exact"))
        EXPECT_EQ(result->dump(), substitute(*exact).dump()) << what;
    if (const Json *save = line.find("save"))
        for (const Json::Member &m : save->asObject()) {
            const Json *value = pointer(*result, m.value.asString());
            if (!value)
                ADD_FAILURE() << what << ": nothing at " << m.value.asString() << " to save";
            else
                saved[m.key] = *value;
        }
}

int ScriptClient::run(const std::filesystem::path &script)
{
    std::ifstream in(script);
    if (!in) {
        ADD_FAILURE() << "cannot read " << script;
        return 0;
    }
    int steps = 0;
    std::string text;
    for (int number = 1; std::getline(in, text); ++number) {
        if (text.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        const std::string where = script.filename().string() + ":" + std::to_string(number);
        auto line = Json::parse(text);
        if (!line) {
            ADD_FAILURE() << where << ": " << line.error().message << " at " << line.error().offset;
            continue;
        }
        step(*line, where);
        ++steps;
    }
    return steps;
}

}
