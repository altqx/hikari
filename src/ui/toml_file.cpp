#include "toml_file.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>

namespace hikari::ui {

using application::checkFileVersion;
using application::SettingType;
using application::SettingValue;
using StringList = std::vector<std::string>;

std::variant<toml::table, ParseFailure> parseTomlFile(std::string_view text, std::int64_t supported)
{
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error &error) {
        const auto &place = error.source().begin;
        return ParseFailure{static_cast<int>(place.line), static_cast<int>(place.column),
                            std::string(error.description())};
    }
    std::optional<std::int64_t> version;
    if (const auto *node = table.get("version"); node && node->is_integer())
        version = node->value<std::int64_t>();
    else if (node)
        return ParseFailure{static_cast<int>(node->source().begin.line), static_cast<int>(node->source().begin.column),
                            "Its version is not a whole number."};
    if (auto failure = checkFileVersion(version, supported))
        return *failure;
    table.erase("version");
    return table;
}

namespace {

constexpr auto kFlags = toml::toml_formatter::default_flags & ~toml::format_flags::allow_literal_strings;

}

std::string formatToml(const toml::node &node)
{
    std::ostringstream out;
    node.visit([&](const auto &n) { out << toml::toml_formatter(n, kFlags); });
    return out.str();
}

std::string formatTomlFile(const toml::table &table, std::int64_t version)
{
    std::ostringstream out;
    out << "version = " << version << "\n";
    if (!table.empty())
        out << "\n" << toml::toml_formatter(table, kFlags) << "\n";
    return out.str();
}

std::vector<std::string_view> idSegments(std::string_view id)
{
    std::vector<std::string_view> out;
    for (std::size_t start = 0;;) {
        const auto dot = id.find('.', start);
        out.push_back(id.substr(start, dot == std::string_view::npos ? dot : dot - start));
        if (dot == std::string_view::npos)
            return out;
        start = dot + 1;
    }
}

std::optional<SettingValue> fromToml(const toml::node &node)
{
    if (const auto *v = node.as_boolean())
        return v->get();
    if (const auto *v = node.as_integer())
        return v->get();
    if (const auto *v = node.as_floating_point())
        return static_cast<std::int64_t>(std::llround(v->get()));
    if (const auto *v = node.as_string())
        return v->get();
    if (const auto *array = node.as_array()) {
        StringList out;
        for (const auto &entry : *array) {
            if (const auto *s = entry.as_string())
                out.push_back(s->get());
            else if (const auto value = fromToml(entry); value && !std::holds_alternative<StringList>(*value))
                out.push_back(std::get<std::string>(application::convertSetting(*value, SettingType::String)));
        }
        return out;
    }
    return std::nullopt;
}

void putToml(toml::table &table, std::string_view key, const SettingValue &value)
{
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, StringList>) {
                toml::array array;
                for (const auto &entry : v)
                    array.push_back(entry);
                table.insert_or_assign(key, std::move(array));
            } else
                table.insert_or_assign(key, v);
        },
        value);
}

toml::table *tomlParent(toml::table &root, std::string_view id, bool create)
{
    const auto parts = idSegments(id);
    toml::table *table = &root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        auto *next = table->get(parts[i]);
        if (!next || !next->is_table()) {
            if (!create)
                return nullptr;
            table->insert_or_assign(parts[i], toml::table());
            next = table->get(parts[i]);
        }
        table = next->as_table();
    }
    return table;
}

const toml::node *tomlNode(const toml::table &root, std::string_view id)
{
    auto *parent = tomlParent(const_cast<toml::table &>(root), id, false);
    return parent ? parent->get(idSegments(id).back()) : nullptr;
}

void eraseToml(toml::table &root, std::string_view id)
{
    const auto parts = idSegments(id);
    std::vector<toml::table *> chain{&root};
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        auto *next = chain.back()->get(parts[i]);
        if (!next || !next->is_table())
            return;
        chain.push_back(next->as_table());
    }
    chain.back()->erase(parts.back());
    for (std::size_t i = chain.size() - 1; i > 0 && chain[i]->empty(); --i)
        chain[i - 1]->erase(parts[i - 1]);
}

namespace {

constexpr auto npos = std::string_view::npos;

bool bareKeyChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::size_t skipBlank(std::string_view t, std::size_t i)
{
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
        ++i;
    return i;
}

std::size_t afterLine(std::string_view t, std::size_t i)
{
    const auto newline = t.find('\n', i);
    return newline == npos ? t.size() : newline + 1;
}

// A dotted key at `i` into `out`; the position after it, or npos.
std::size_t readKey(std::string_view t, std::size_t i, std::vector<std::string> &out)
{
    for (;;) {
        i = skipBlank(t, i);
        if (i >= t.size())
            return npos;
        if (t[i] == '"' || t[i] == '\'') {
            const char quote = t[i];
            const auto close = t.find_first_of(quote == '"' ? "\"\\\n" : "'\n", i + 1);
            if (close == npos || t[close] != quote)
                return npos;
            out.emplace_back(t.substr(i + 1, close - i - 1));
            i = close + 1;
        } else {
            const auto start = i;
            while (i < t.size() && bareKeyChar(t[i]))
                ++i;
            if (i == start)
                return npos;
            out.emplace_back(t.substr(start, i - start));
        }
        i = skipBlank(t, i);
        if (i >= t.size() || t[i] != '.')
            return i;
        ++i;
    }
}

std::size_t stringEnd(std::string_view t, std::size_t i)
{
    const char quote = t[i];
    const std::string triple(3, quote);
    if (t.substr(i, 3) == triple) {
        for (auto at = i + 3; (at = t.find(triple, at)) != npos; ++at) {
            std::size_t slashes = 0;
            while (quote == '"' && at - slashes > i + 3 && t[at - slashes - 1] == '\\')
                ++slashes;
            if (slashes % 2 == 0) {
                at += 3;
                while (at < t.size() && t[at] == quote && at - i < 8)
                    ++at;
                return at;
            }
        }
        return npos;
    }
    for (auto at = i + 1; at < t.size() && t[at] != '\n'; ++at) {
        if (quote == '"' && t[at] == '\\')
            ++at;
        else if (t[at] == quote)
            return at + 1;
    }
    return npos;
}

// The end of the value at `i`, before its trailing blanks and comment; npos when it never ends.
std::size_t valueEnd(std::string_view t, std::size_t i)
{
    int depth = 0;
    std::size_t last = i;
    while (i < t.size()) {
        const char c = t[i];
        if (c == '"' || c == '\'') {
            i = stringEnd(t, i);
            if (i == npos)
                return npos;
            last = i;
        } else if (c == '#' || c == '\n' || c == '\r') {
            if (depth == 0)
                return last;
            i = c == '#' ? t.find('\n', i) : i + 1;
            if (i == npos)
                return npos;
        } else if (c == ' ' || c == '\t') {
            ++i;
        } else {
            depth += int(c == '[' || c == '{') - int(c == ']' || c == '}');
            last = ++i;
        }
    }
    return depth == 0 ? last : npos;
}

std::string joined(const std::vector<std::string> &segments)
{
    std::string out;
    for (const auto &segment : segments)
        out += (out.empty() ? "" : ".") + segment;
    return out;
}

struct TomlEntry {
    std::string id;
    std::size_t lineBegin;
    std::size_t valueBegin;
    std::size_t valueEnd;
    std::size_t lineEnd;
};

struct TomlTable {
    std::string name;
    std::size_t end; // after its header or its last entry
};

struct TomlLayout {
    std::vector<TomlEntry> entries;
    std::vector<TomlTable> tables;
};

std::optional<TomlLayout> scanToml(std::string_view t)
{
    TomlLayout layout;
    layout.tables.push_back({"", 0});
    std::optional<std::size_t> table = 0;
    for (std::size_t i = 0; i < t.size();) {
        const auto lineBegin = i;
        i = skipBlank(t, i);
        if (i >= t.size())
            break;
        if (t[i] == '\n' || t[i] == '\r' || t[i] == '#') {
            i = afterLine(t, i);
            continue;
        }
        std::vector<std::string> key;
        if (t[i] == '[') {
            const bool array = t.substr(i, 2) == "[[";
            const std::string_view close = array ? "]]" : "]";
            i = readKey(t, i + close.size(), key);
            if (i == npos || t.substr(i, close.size()) != close)
                return std::nullopt;
            i = afterLine(t, i);
            table.reset();
            if (!array) {
                layout.tables.push_back({joined(key), i});
                table = layout.tables.size() - 1;
            }
            continue;
        }
        i = readKey(t, i, key);
        if (i == npos || i >= t.size() || t[i] != '=')
            return std::nullopt;
        const auto valueBegin = skipBlank(t, i + 1);
        const auto end = valueEnd(t, valueBegin);
        if (end == npos || end == valueBegin)
            return std::nullopt;
        i = afterLine(t, end);
        if (!table)
            continue;
        auto &owner = layout.tables[*table];
        layout.entries.push_back(
            {owner.name.empty() ? joined(key) : owner.name + "." + joined(key), lineBegin, valueBegin, end, i});
        owner.end = i;
    }
    return layout;
}

std::string valueText(const SettingValue &value)
{
    toml::table holder;
    putToml(holder, "v", value);
    return formatToml(*holder.get("v"));
}


}

namespace {

std::optional<std::string> patchToml(std::string_view text, std::string_view id,
                                     const std::optional<std::string> &value,
                                     const std::function<bool(const toml::node *)> &holds)
{
    const auto layout = scanToml(text);
    if (!layout)
        return std::nullopt;
    const std::string newline = text.find("\r\n") != npos ? "\r\n" : "\n";
    std::string out(text);
    const auto entry = std::ranges::find(layout->entries, id, &TomlEntry::id);
    if (entry != layout->entries.end()) {
        if (value)
            out.replace(entry->valueBegin, entry->valueEnd - entry->valueBegin, *value);
        else
            out.erase(entry->lineBegin, entry->lineEnd - entry->lineBegin);
    } else if (value) {
        const auto dot = id.rfind('.');
        if (dot == npos)
            return std::nullopt;
        const std::string parent(id.substr(0, dot));
        const std::string line = std::string(id.substr(dot + 1)) + " = " + *value + newline;
        const auto table = std::ranges::find(layout->tables, parent, &TomlTable::name);
        if (table != layout->tables.end()) {
            const bool ended = table->end == 0 || text.substr(0, table->end).ends_with('\n');
            out.insert(table->end, (ended ? "" : newline) + line);
        } else {
            if (!out.empty() && !out.ends_with('\n'))
                out += newline;
            out += (out.empty() ? "" : newline) + "[" + parent + "]" + newline + line;
        }
    }
    try {
        const toml::table parsed = toml::parse(out);
        if (!holds(tomlNode(parsed, id)))
            return std::nullopt;
    } catch (const toml::parse_error &) {
        return std::nullopt;
    }
    return out;
}

}

std::optional<std::string> patchTomlSetting(std::string_view text, std::string_view id,
                                            const std::optional<SettingValue> &value)
{
    return patchToml(text, id, value ? std::optional(valueText(*value)) : std::nullopt, [&](const toml::node *node) {
        return value ? node && fromToml(*node) == value : node == nullptr;
    });
}

std::optional<std::string> patchTomlNode(std::string_view text, std::string_view id, const toml::node *value)
{
    return patchToml(text, id, value ? std::optional(formatToml(*value)) : std::nullopt, [&](const toml::node *node) {
        return value ? node && formatToml(*node) == formatToml(*value) : node == nullptr;
    });
}

}
