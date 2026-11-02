#include "hikari/api/schema.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace hikari::api {

namespace {

constexpr std::string_view kRefPrefix = "#/$defs/";

const Json *resolve(const Json &ref, const Json &defs)
{
    if (!ref.isString() || !ref.asString().starts_with(kRefPrefix))
        return nullptr;
    return defs.find(std::string_view(ref.asString()).substr(kRefPrefix.size()));
}

bool hasType(std::string_view type, const Json &value)
{
    if (type == "null")
        return value.isNull();
    if (type == "boolean")
        return value.isBool();
    if (type == "integer")
        return value.isInt();
    if (type == "number")
        return value.isNumber();
    if (type == "string")
        return value.isString();
    if (type == "array")
        return value.isArray();
    if (type == "object")
        return value.isObject();
    return false;
}

std::size_t codePoints(const std::string &s)
{
    return std::size_t(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::string escapePointer(std::string_view key)
{
    std::string out;
    for (const char c : key) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
    return out;
}

class Validator {
public:
    explicit Validator(const Json &defs) : m_defs(defs) {}

    std::optional<SchemaFailure> check(const Json &schema, const Json &value, const std::string &path, int depth) const
    {
        if (depth > Json::kMaxDepth)
            return SchemaFailure{path, "schema nests too deeply"};
        if (schema.isBool())
            return schema.asBool() ? std::nullopt : std::optional(SchemaFailure{path, "no value is allowed here"});
        if (const Json *ref = schema.find("$ref")) {
            const Json *target = resolve(*ref, m_defs);
            if (!target)
                return SchemaFailure{path, "unresolved $ref"};
            if (auto failure = check(*target, value, path, depth + 1))
                return failure;
        }
        if (const Json *type = schema.find("type")) {
            bool ok = false;
            if (type->isString())
                ok = hasType(type->asString(), value);
            else
                for (const Json &t : type->asArray())
                    ok = ok || hasType(t.asString(), value);
            if (!ok)
                return SchemaFailure{path, "expected " + type->dump()};
        }
        if (const Json *c = schema.find("const"); c && !(*c == value))
            return SchemaFailure{path, "expected " + c->dump()};
        if (const Json *e = schema.find("enum")) {
            const auto &options = e->asArray();
            if (std::find(options.begin(), options.end(), value) == options.end())
                return SchemaFailure{path, "expected one of " + e->dump()};
        }
        if (value.isNumber()) {
            if (const Json *m = schema.find("minimum"); m && value.asNumber() < m->asNumber())
                return SchemaFailure{path, "below the minimum " + m->dump()};
            if (const Json *m = schema.find("maximum"); m && value.asNumber() > m->asNumber())
                return SchemaFailure{path, "above the maximum " + m->dump()};
        }
        if (value.isString()) {
            const std::size_t length = codePoints(value.asString());
            if (const Json *m = schema.find("minLength"); m && length < std::size_t(m->asInt()))
                return SchemaFailure{path, "shorter than " + m->dump() + " characters"};
            if (const Json *m = schema.find("maxLength"); m && length > std::size_t(m->asInt()))
                return SchemaFailure{path, "longer than " + m->dump() + " characters"};
        }
        if (value.isArray()) {
            const auto &items = value.asArray();
            if (const Json *m = schema.find("minItems"); m && items.size() < std::size_t(m->asInt()))
                return SchemaFailure{path, "fewer than " + m->dump() + " items"};
            if (const Json *m = schema.find("maxItems"); m && items.size() > std::size_t(m->asInt()))
                return SchemaFailure{path, "more than " + m->dump() + " items"};
            if (const Json *itemSchema = schema.find("items"))
                for (std::size_t i = 0; i < items.size(); ++i)
                    if (auto failure = check(*itemSchema, items[i], path + "/" + std::to_string(i), depth + 1))
                        return failure;
        }
        if (value.isObject()) {
            const Json *properties = schema.find("properties");
            if (const Json *required = schema.find("required"))
                for (const Json &key : required->asArray())
                    if (!value.find(key.asString()))
                        return SchemaFailure{path, "missing \"" + key.asString() + "\""};
            const Json *additional = schema.find("additionalProperties");
            for (const Json::Member &member : value.asObject()) {
                const std::string at = path + "/" + escapePointer(member.key);
                if (const Json *property = properties ? properties->find(member.key) : nullptr) {
                    if (auto failure = check(*property, member.value, at, depth + 1))
                        return failure;
                } else if (additional) {
                    if (additional->isBool() && !additional->asBool())
                        return SchemaFailure{at, "unknown property"};
                    if (auto failure = check(*additional, member.value, at, depth + 1))
                        return failure;
                }
            }
        }
        if (const Json *any = schema.find("anyOf")) {
            const auto &options = any->asArray();
            if (std::none_of(options.begin(), options.end(),
                             [&](const Json &option) { return !check(option, value, path, depth + 1); }))
                return SchemaFailure{path, "matches none of anyOf"};
        }
        if (const Json *one = schema.find("oneOf")) {
            const auto &options = one->asArray();
            const auto matched = std::count_if(options.begin(), options.end(),
                                               [&](const Json &option) { return !check(option, value, path, depth + 1); });
            if (matched > 1)
                return SchemaFailure{path, "matches more than one of oneOf"};
            if (matched == 0) {
                if (const Json *chosen = discriminated(options, value))
                    return check(*chosen, value, path, depth + 1);
                return SchemaFailure{path, "matches none of oneOf"};
            }
        }
        return std::nullopt;
    }

private:
    // The one option a const property of `value` picks, as "op" picks an edit's kind.
    static const Json *discriminated(const Json::Array &options, const Json &value)
    {
        if (!value.isObject())
            return nullptr;
        const Json *chosen = nullptr;
        for (const Json &option : options) {
            const Json *properties = option.find("properties");
            if (!properties || !properties->isObject())
                continue;
            for (const Json::Member &p : properties->asObject()) {
                const Json *c = p.value.find("const");
                const Json *given = value.find(p.key);
                if (c && given && *c == *given) {
                    if (chosen)
                        return nullptr;
                    chosen = &option;
                }
            }
        }
        return chosen;
    }

    const Json &m_defs;
};

constexpr std::array kTypes{"null", "boolean", "integer", "number", "string", "array", "object"};

bool isTypeName(const Json &t)
{
    return t.isString() && std::find(kTypes.begin(), kTypes.end(), t.asString()) != kTypes.end();
}

bool isCount(const Json &v)
{
    return v.isInt() && v.asInt() >= 0;
}

std::optional<std::string> checkNode(const Json &schema, const Json &defs, const std::string &path, int depth)
{
    if (depth > Json::kMaxDepth)
        return path + ": nests too deeply";
    if (schema.isBool())
        return std::nullopt;
    if (!schema.isObject())
        return path + ": a schema is an object or a boolean";
    const auto sub = [&](const Json &s, const std::string &at) { return checkNode(s, defs, at, depth + 1); };
    for (const Json::Member &m : schema.asObject()) {
        const std::string at = path + "/" + escapePointer(m.key);
        const Json &v = m.value;
        if (m.key == "description" || m.key == "title") {
            if (!v.isString())
                return at + ": not a string";
        } else if (m.key == "default" || m.key == "const") {
        } else if (m.key == "$ref") {
            if (!resolve(v, defs))
                return at + ": unresolved";
        } else if (m.key == "type") {
            const bool ok = isTypeName(v) || (v.isArray() && !v.asArray().empty() &&
                                              std::all_of(v.asArray().begin(), v.asArray().end(), isTypeName));
            if (!ok)
                return at + ": not a type";
        } else if (m.key == "enum") {
            if (!v.isArray() || v.asArray().empty())
                return at + ": not a non-empty array";
        } else if (m.key == "minimum" || m.key == "maximum") {
            if (!v.isNumber())
                return at + ": not a number";
        } else if (m.key == "minLength" || m.key == "maxLength" || m.key == "minItems" || m.key == "maxItems") {
            if (!isCount(v))
                return at + ": not a count";
        } else if (m.key == "required") {
            if (!v.isArray() || !std::all_of(v.asArray().begin(), v.asArray().end(), [](const Json &k) { return k.isString(); }))
                return at + ": not an array of names";
        } else if (m.key == "properties") {
            if (!v.isObject())
                return at + ": not an object";
            for (const Json::Member &p : v.asObject())
                if (auto problem = sub(p.value, at + "/" + escapePointer(p.key)))
                    return problem;
        } else if (m.key == "items" || m.key == "additionalProperties") {
            if (auto problem = sub(v, at))
                return problem;
        } else if (m.key == "oneOf" || m.key == "anyOf") {
            if (!v.isArray() || v.asArray().empty())
                return at + ": not a non-empty array";
            for (std::size_t i = 0; i < v.asArray().size(); ++i)
                if (auto problem = sub(v.asArray()[i], at + "/" + std::to_string(i)))
                    return problem;
        } else {
            return at + ": unsupported keyword";
        }
    }
    return std::nullopt;
}

}

std::optional<SchemaFailure> validate(const Json &schema, const Json &value, const Json &defs)
{
    return Validator(defs).check(schema, value, "", 0);
}

std::optional<std::string> checkSchema(const Json &schema, const Json &defs)
{
    return checkNode(schema, defs, "", 0);
}

}
