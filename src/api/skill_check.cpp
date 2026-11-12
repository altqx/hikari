#include "hikari/api/skill_check.h"

#include "hikari/api/json.h"

#include <algorithm>
#include <vector>

namespace hikari::api {

namespace {

bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trimmed(std::string_view s)
{
    while (!s.empty() && space(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && space(s.back()))
        s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> out;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        out.push_back(line);
        if (end == std::string_view::npos)
            return out;
        start = end + 1;
    }
}

std::string replaced(std::string s, std::string_view from, std::string_view to)
{
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

std::string unquoted(const std::string &value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        const auto parsed = Json::parse(value);
        return parsed && parsed->isString() ? parsed->asString() : value.substr(1, value.size() - 2);
    }
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
        return replaced(value.substr(1, value.size() - 2), "''", "'");
    return value;
}

std::string block(std::string_view style, const std::vector<std::string_view> &continued)
{
    std::string joined;
    for (std::size_t i = 0; i < continued.size(); ++i)
        joined += (i ? "\n" : "") + trimmed(continued[i]);
    const std::string kept = trimmed(joined);
    if (style.starts_with('|'))
        return kept;
    std::string out;
    std::string paragraph;
    std::size_t newlines = 0;
    for (const char c : kept + '\0') {
        if (c == '\n') {
            ++newlines;
            continue;
        }
        if (newlines >= 2 || c == '\0') {
            out += (out.empty() ? "" : "\n") + paragraph;
            paragraph.clear();
        } else if (newlines == 1) {
            paragraph += ' ';
        }
        newlines = 0;
        if (c != '\0')
            paragraph += c;
    }
    return out;
}

bool keyChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// `key: value` as /^([A-Za-z][\w-]*):(?:\s+(.*))?$/ reads it.
std::optional<std::pair<std::string, std::string>> field(std::string_view line)
{
    if (line.empty() || !((line[0] >= 'A' && line[0] <= 'Z') || (line[0] >= 'a' && line[0] <= 'z')))
        return std::nullopt;
    std::size_t colon = 1;
    while (colon < line.size() && keyChar(line[colon]))
        ++colon;
    if (colon >= line.size() || line[colon] != ':')
        return std::nullopt;
    const std::string_view rest = line.substr(colon + 1);
    if (!rest.empty() && !space(rest.front()))
        return std::nullopt;
    return std::pair{std::string(line.substr(0, colon)), trimmed(rest)};
}

bool blockStyle(std::string_view v)
{
    return (v.size() == 1 || (v.size() == 2 && (v[1] == '+' || v[1] == '-'))) && (v[0] == '>' || v[0] == '|');
}

std::string collapsed(std::string_view s)
{
    std::string out;
    bool gap = false;
    for (const char c : s) {
        if (space(c)) {
            gap = true;
            continue;
        }
        if (gap && !out.empty())
            out += ' ';
        gap = false;
        out += c;
    }
    return out;
}

std::string firstCodePoints(const std::string &s, std::size_t count)
{
    std::size_t at = 0;
    for (std::size_t n = 0; at < s.size() && n < count; ++n)
        do
            ++at;
        while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80);
    return s.substr(0, at);
}

}

std::optional<SkillText> parseSkill(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    const std::vector<std::string_view> all = lines(text);
    if (trimmed(all[0]) != "---")
        return std::nullopt;
    std::size_t end = 1;
    while (end < all.size() && trimmed(all[end]) != "---")
        ++end;
    if (end == all.size())
        return std::nullopt;
    SkillText out;
    for (std::size_t i = 1; i < end; ++i) {
        const auto kv = field(all[i]);
        if (!kv)
            continue;
        if (blockStyle(kv->second)) {
            std::vector<std::string_view> continued;
            while (i + 1 < end && ((!all[i + 1].empty() && space(all[i + 1][0])) || trimmed(all[i + 1]).empty()))
                continued.push_back(all[++i]);
            out.fields[kv->first] = block(kv->second, continued);
        } else if (!kv->second.empty()) {
            out.fields[kv->first] = unquoted(kv->second);
        }
    }
    std::string body;
    for (std::size_t i = end + 1; i < all.size(); ++i)
        body += std::string(i > end + 1 ? "\n" : "") + std::string(all[i]);
    out.body = trimmed(body);
    return out;
}

SkillCheck checkSkill(std::string_view folder, std::string_view text)
{
    const auto parsed = parseSkill(text);
    if (!parsed)
        return {SkillProblem::NoFrontmatter, {}, {}};
    const auto name = parsed->fields.find("name");
    if (name != parsed->fields.end() && name->second != folder)
        return {SkillProblem::NameMismatch, {}, name->second};
    const auto description = parsed->fields.find("description");
    const std::string oneLine = description == parsed->fields.end() ? std::string() : collapsed(description->second);
    if (oneLine.empty())
        return {SkillProblem::NoDescription, {}, {}};
    return {SkillProblem::None, firstCodePoints(oneLine, kMaxSkillDescription), {}};
}

}
