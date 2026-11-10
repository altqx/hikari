#include "hikari/api/reference.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::api {

namespace {

constexpr std::array kTypeKeywords{"type", "$ref", "anyOf", "oneOf", "enum", "const", "items", "properties", "required",
                                   "additionalProperties", "description"};

std::string text(const Json &value)
{
    return value.isString() ? value.asString() : value.dump();
}

std::string code(std::string_view value)
{
    return "`" + std::string(value) + "`";
}

std::string cell(std::string value)
{
    std::string out;
    for (const char c : value) {
        if (c == '|')
            out += "\\|";
        else if (c == '\n')
            out += ' ';
        else
            out += c;
    }
    return out;
}

std::string anchor(std::string_view name)
{
    std::string out;
    for (const char c : name)
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            out += c;
        else if (c >= 'A' && c <= 'Z')
            out += char(c - 'A' + 'a');
    return out;
}

std::string joined(const std::vector<std::string> &parts, std::string_view last)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            out += i + 1 == parts.size() ? last : std::string_view(", ");
        out += parts[i];
    }
    return out;
}

bool hasProperties(const Json &schema)
{
    const Json *properties = schema.find("properties");
    return properties && properties->isObject() && !properties->asObject().empty();
}

std::string typeOf(const Json &schema);

std::string alternatives(const Json &list)
{
    std::vector<std::string> parts;
    for (const Json &alternative : list.asArray())
        parts.push_back(typeOf(alternative));
    return joined(parts, " or ");
}

std::string typeOf(const Json &schema)
{
    if (!schema.isObject())
        return "any";
    if (const Json *ref = schema.find("$ref"); ref && ref->isString()) {
        const std::string name = ref->asString().substr(ref->asString().rfind('/') + 1);
        return "[" + name + "](#" + anchor(name) + ")";
    }
    if (const Json *list = schema.find("anyOf"))
        return alternatives(*list);
    if (const Json *list = schema.find("oneOf"))
        return alternatives(*list);
    if (const Json *value = schema.find("const"))
        return code(value->dump());
    if (const Json *values = schema.find("enum")) {
        std::vector<std::string> parts;
        for (const Json &value : values->asArray())
            parts.push_back(code(text(value)));
        return "one of " + joined(parts, " or ");
    }
    const Json *type = schema.find("type");
    if (!type)
        return "any";
    if (type->isArray()) {
        std::vector<std::string> parts;
        for (const Json &name : type->asArray())
            parts.push_back(text(name));
        return joined(parts, " or ");
    }
    const std::string &name = type->asString();
    if (name == "array") {
        const Json *items = schema.find("items");
        if (!items)
            return "array";
        return hasProperties(*items) ? "array of objects" : "array of " + typeOf(*items);
    }
    if (name == "object" && !hasProperties(schema))
        if (const Json *map = schema.find("additionalProperties"); map && map->isObject())
            return "object of " + typeOf(*map) + " by name";
    return name;
}

std::string limits(const Json &schema)
{
    std::vector<std::string> parts;
    const Json *minimum = schema.find("minimum");
    const Json *maximum = schema.find("maximum");
    if (minimum && maximum)
        parts.push_back("From " + minimum->dump() + " to " + maximum->dump() + ".");
    else if (minimum)
        parts.push_back("At least " + minimum->dump() + ".");
    else if (maximum)
        parts.push_back("At most " + maximum->dump() + ".");
    const auto count = [&parts, &schema](const char *least, const char *most, std::string_view unit) {
        const Json *low = schema.find(least);
        const Json *high = schema.find(most);
        if (low && high)
            parts.push_back(low->dump() + " to " + high->dump() + " " + std::string(unit) + ".");
        else if (low && low->isInt() && low->asInt() == 1 && unit == "characters")
            parts.push_back("Not empty.");
        else if (low)
            parts.push_back("At least " + low->dump() + " " + std::string(unit) + ".");
        else if (high)
            parts.push_back("At most " + high->dump() + " " + std::string(unit) + ".");
    };
    count("minLength", "maxLength", "characters");
    count("minItems", "maxItems", "items");
    constexpr std::array kLimits{"minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems"};
    for (const Json::Member &member : schema.asObject()) {
        const std::string_view key = member.key;
        if (std::ranges::find(kTypeKeywords, key) == kTypeKeywords.end() &&
            std::ranges::find(kLimits, key) == kLimits.end())
            parts.push_back(code(key) + ": " + code(member.value.dump()) + ".");
    }
    return joined(parts, " ");
}

std::string meaning(const Json &schema)
{
    std::string out;
    if (const Json *description = schema.find("description"); description && description->isString())
        out = description->asString();
    const std::string extra = limits(schema);
    if (!extra.empty())
        out += (out.empty() ? "" : " ") + extra;
    return out;
}

bool isRequired(const Json &parent, std::string_view name)
{
    const Json *required = parent.find("required");
    if (!required || !required->isArray())
        return false;
    return std::ranges::any_of(required->asArray(), [name](const Json &n) { return n.isString() && n.asString() == name; });
}

void rows(const Json &object, const std::string &prefix, std::string &out)
{
    if (!hasProperties(object))
        return;
    for (const Json::Member &property : object.find("properties")->asObject()) {
        const std::string path = prefix + property.key;
        const Json &schema = property.value;
        out += "| " + code(path) + " | " + cell(typeOf(schema)) + " | " + (isRequired(object, property.key) ? "yes" : "") +
               " | " + cell(meaning(schema)) + " |\n";
        if (hasProperties(schema))
            rows(schema, path + ".", out);
        else if (const Json *items = schema.find("items"); items && hasProperties(*items))
            rows(*items, path + "[].", out);
    }
}

void table(const Json &object, std::string_view column, std::string &out)
{
    out += "| Field | Type | " + std::string(column) + " | Meaning |\n| --- | --- | --- | --- |\n";
    rows(object, "", out);
}

std::string label(const Json &alternative)
{
    if (hasProperties(alternative))
        for (const Json::Member &property : alternative.find("properties")->asObject())
            if (const Json *value = property.value.find("const"))
                return code(property.key) + " " + code(text(*value));
    return typeOf(alternative);
}

void schemaBlock(const Json &schema, std::string_view title, std::string_view column, std::string &out)
{
    const std::string named = title.empty() ? std::string() : "**" + std::string(title) + "**";
    const Json *description = schema.find("description");
    const std::string about = description && description->isString() ? description->asString() : std::string();
    if (hasProperties(schema)) {
        if (!named.empty())
            out += named + "\n\n";
        if (!about.empty())
            out += about + "\n\n";
        table(schema, column, out);
        out += "\n";
        return;
    }
    const Json *list = schema.find("oneOf");
    if (!list)
        list = schema.find("anyOf");
    if (list && std::ranges::any_of(list->asArray(), hasProperties)) {
        out += (named.empty() ? "One of:" : named + ", one of:") + "\n\n";
        for (const Json &alternative : list->asArray()) {
            out += "- " + label(alternative);
            if (const std::string more = meaning(alternative); !more.empty())
                out += ": " + more;
            out += "\n";
        }
        out += "\n";
        for (const Json &alternative : list->asArray())
            if (hasProperties(alternative)) {
                out += "*" + label(alternative) + "*\n\n";
                table(alternative, column, out);
                out += "\n";
            }
        return;
    }
    const std::string type = typeOf(schema);
    const std::string more = meaning(schema);
    if (type == "object" && more.empty()) {
        out += (named.empty() ? "An object" : named) + ": none.\n\n";
        return;
    }
    out += (named.empty() ? std::string("Type") : named) + ": " + type + ".";
    if (!more.empty())
        out += " " + more;
    out += "\n\n";
}

std::string since(const Json &entry)
{
    std::string out = "Since version " + entry.find("since")->dump() + ".";
    if (const Json *deprecated = entry.find("deprecatedIn"))
        out += " Deprecated in version " + deprecated->dump() + ".";
    return out;
}

std::string capitalised(std::string word)
{
    if (!word.empty() && word[0] >= 'a' && word[0] <= 'z')
        word[0] = char(word[0] - 'a' + 'A');
    return word;
}

}

std::string referenceMarkdown(const Json &reference)
{
    const std::string version = reference.find("version")->dump();
    std::string out = "# Hikari API reference\n\n";
    out += "Version " + version +
           " of the Hikari API, generated from HikariSub's own registry: the same reference `hikarisub --export-docs` "
           "writes to `api.json`. A plugin calls these operations with `hikari.call(operation, params)`, or "
           "`api.call` in a QML panel, and listens to the events with `hikari.events.on(name, fn)`.\n\n";
    out += "## Operations\n\n| Operation | Mark | Since | Summary |\n| --- | --- | --- | --- |\n";
    const Json::Array &operations = reference.find("operations")->asArray();
    for (const Json &op : operations)
        out += "| " + code(op.find("name")->asString()) + " | " + op.find("access")->asString() + " | " +
               op.find("since")->dump() + " | " + cell(op.find("summary")->asString()) + " |\n";
    out += "\n";
    for (const Json &op : operations) {
        out += "### " + code(op.find("name")->asString()) + "\n\n";
        out += capitalised(op.find("access")->asString()) + ". " + since(op) + "\n\n";
        out += op.find("summary")->asString() + "\n\n";
        schemaBlock(*op.find("params"), "Parameters", "Required", out);
        schemaBlock(*op.find("result"), "Result", "Always", out);
    }
    out += "## Events\n\n";
    for (const Json &event : reference.find("events")->asArray()) {
        out += "### " + code(event.find("name")->asString()) + "\n\n";
        out += since(event) + "\n\n" + event.find("summary")->asString() + "\n\n";
        schemaBlock(*event.find("data"), "Data", "Always", out);
    }
    out += "## Types\n\n";
    for (const Json::Member &def : reference.find("$defs")->asObject()) {
        out += "### " + def.key + "\n\n";
        schemaBlock(def.value, {}, "Required", out);
    }
    while (out.size() > 1 && out.ends_with("\n\n"))
        out.pop_back();
    return out;
}

}
