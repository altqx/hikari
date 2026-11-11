#include "hikari/application/dependency_control.h"

#include <cctype>
#include <optional>

namespace hikari::application {

namespace {

constexpr std::string_view kDependencyControl = "l0.DependencyControl";

struct Token {
    enum Kind { String, Open, Close, Comma, Equals, Name, Other } kind = Other;
    std::string text;
};

std::vector<Token> tokens(std::string_view s)
{
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (s.substr(i, 2) == "--") {
            if (s.substr(i + 2, 2) == "[[") {
                const auto end = s.find("]]", i + 4);
                i = end == std::string_view::npos ? s.size() : end + 2;
            } else {
                const auto end = s.find('\n', i);
                i = end == std::string_view::npos ? s.size() : end + 1;
            }
        } else if (c == '"' || c == '\'') {
            std::string text;
            for (++i; i < s.size() && s[i] != c && s[i] != '\n'; ++i) {
                if (s[i] == '\\' && i + 1 < s.size())
                    ++i;
                text += s[i];
            }
            ++i;
            out.push_back({Token::String, std::move(text)});
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string text;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
                text += s[i++];
            out.push_back({Token::Name, std::move(text)});
        } else {
            const Token::Kind kind = c == '{' ? Token::Open : c == '}' ? Token::Close : c == ',' ? Token::Comma
                                     : c == '=' && s.substr(i, 2) != "==" ? Token::Equals : Token::Other;
            if (kind != Token::Other || !std::isspace(static_cast<unsigned char>(c)))
                out.push_back({kind, std::string(1, c)});
            i += c == '=' && kind == Token::Other ? 2 : 1;
        }
    }
    return out;
}

bool looksLikeModule(std::string_view s)
{
    if (s == kDependencyControl || s.find('.') == std::string_view::npos || s.front() == '.' || s.back() == '.')
        return false;
    for (const std::string_view suffix : {".lua", ".moon", ".json"})
        if (s.ends_with(suffix))
            return false;
    for (const char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-')
            return false;
    return true;
}

// The first table after `name{` or `name({`, where `name` holds require("l0.DependencyControl").
std::optional<std::size_t> recordStart(const std::vector<Token> &t)
{
    std::string holder;
    for (std::size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i].kind == Token::String && t[i].text == kDependencyControl) {
            for (std::size_t j = i; j-- > 0;)
                if (t[j].kind == Token::Equals) {
                    if (j > 0 && t[j - 1].kind == Token::Name)
                        holder = t[j - 1].text;
                    break;
                }
            break;
        }
    }
    if (holder.empty())
        return std::nullopt;
    for (std::size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i].kind != Token::Name || t[i].text != holder)
            continue;
        if (t[i + 1].kind == Token::Open)
            return i + 1;
        if (t[i + 1].text == "(" && i + 2 < t.size() && t[i + 2].kind == Token::Open)
            return i + 2;
    }
    return std::nullopt;
}

}

std::vector<ModuleNeed> dependencyControlNeeds(std::string_view script)
{
    std::vector<ModuleNeed> needs;
    const std::vector<Token> t = tokens(script);
    const auto start = recordStart(t);
    if (!start)
        return needs;
    int depth = 0;
    for (std::size_t i = *start; i < t.size(); ++i) {
        if (t[i].kind == Token::Open)
            ++depth;
        else if (t[i].kind == Token::Close && --depth == 0)
            break;
        if (t[i].kind != Token::String || !looksLikeModule(t[i].text) || depth < 2)
            continue;
        if (i > 0 && t[i - 1].kind == Token::Equals)
            continue;
        ModuleNeed need{t[i].text, {}};
        if (t[i - 1].kind == Token::Open) {
            for (std::size_t j = i + 1, d = 0; j < t.size(); ++j) {
                if (t[j].kind == Token::Open)
                    ++d;
                else if (t[j].kind == Token::Close && d-- == 0)
                    break;
                if (d == 0 && t[j].kind == Token::Name && t[j].text == "version" && j + 2 < t.size() &&
                    t[j + 1].kind == Token::Equals && t[j + 2].kind == Token::String)
                    need.version = t[j + 2].text;
            }
        }
        needs.push_back(std::move(need));
    }
    return needs;
}

}
