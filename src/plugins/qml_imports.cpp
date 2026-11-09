#include "hikari/plugins/qml_imports.h"

#include <algorithm>
#include <vector>

namespace hikari::plugins {

namespace {

struct Token {
    enum Kind { Word, String, Punct, Newline };
    Kind kind = Punct;
    std::string text;

    bool is(Kind k, std::string_view t) const { return kind == k && text == t; }
};

bool wordChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

std::vector<Token> tokenise(std::string_view s)
{
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            out.push_back({Token::Newline, "\n"});
            ++i;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
        } else if (s.substr(i, 2) == "//") {
            i = std::min(s.find('\n', i), s.size());
        } else if (s.substr(i, 2) == "/*") {
            const std::size_t end = std::min(s.find("*/", i + 2), s.size());
            if (s.substr(i, end - i).find('\n') != std::string_view::npos)
                out.push_back({Token::Newline, "\n"});
            i = std::min(end + 2, s.size());
        } else if (c == '"' || c == '\'' || c == '`') {
            std::string text;
            for (++i; i < s.size() && s[i] != c; ++i) {
                if (s[i] == '\\' && i + 1 < s.size())
                    ++i;
                text += s[i];
            }
            ++i;
            out.push_back({Token::String, std::move(text)});
        } else if (wordChar(c)) {
            const std::size_t start = i;
            while (i < s.size() && wordChar(s[i]))
                ++i;
            out.push_back({Token::Word, std::string(s.substr(start, i - start))});
        } else {
            out.push_back({Token::Punct, std::string(1, c)});
            ++i;
        }
    }
    return out;
}

bool insidePackage(std::string_view file, std::string_view target)
{
    if (target.empty() || target.front() == '/' || target.find_first_of(":\\") != std::string_view::npos)
        return false;
    auto depth = static_cast<std::size_t>(std::ranges::count(file, '/'));
    while (!target.empty()) {
        const std::size_t slash = target.find('/');
        const std::string_view part = target.substr(0, slash);
        target.remove_prefix(slash == std::string_view::npos ? target.size() : slash + 1);
        if (part == "..") {
            if (depth == 0)
                return false;
            --depth;
        } else if (!part.empty() && part != ".") {
            ++depth;
        }
    }
    return true;
}

struct Imports {
    std::string_view file;
    const std::vector<Token> &tokens;

    std::optional<std::string> path(const std::string &target) const
    {
        if (insidePackage(file, target))
            return std::nullopt;
        return "\"" + target + "\"";
    }

    std::optional<std::string> qmlImport(std::size_t i) const
    {
        if (i >= tokens.size())
            return std::string("import");
        if (tokens[i].kind == Token::String)
            return path(tokens[i].text);
        if (tokens[i].kind != Token::Word)
            return std::string("import");
        std::string name = tokens[i].text;
        while (i + 2 < tokens.size() && tokens[i + 1].is(Token::Punct, ".") && tokens[i + 2].kind == Token::Word) {
            name += "." + tokens[i + 2].text;
            i += 2;
        }
        if (std::ranges::find(kQmlPanelModules, name) != kQmlPanelModules.end())
            return std::nullopt;
        return name;
    }

    std::optional<std::string> qml() const
    {
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const Token &t = tokens[i];
            if (t.kind == Token::Newline || t.is(Token::Punct, ";"))
                continue;
            if (t.is(Token::Word, "pragma")) {
                while (i + 1 < tokens.size() && tokens[i + 1].kind != Token::Newline)
                    ++i;
                continue;
            }
            if (!t.is(Token::Word, "import"))
                return std::nullopt;
            if (auto refused = qmlImport(i + 1))
                return refused;
            while (i + 1 < tokens.size() && tokens[i + 1].kind != Token::Newline && !tokens[i + 1].is(Token::Punct, ";"))
                ++i;
        }
        return std::nullopt;
    }

    std::optional<std::string> fromClause(std::size_t i) const
    {
        for (; i < tokens.size() && !tokens[i].is(Token::Punct, ";"); ++i)
            if (tokens[i].is(Token::Word, "from") && i + 1 < tokens.size() && tokens[i + 1].kind == Token::String)
                return path(tokens[i + 1].text);
        return std::nullopt;
    }

    std::optional<std::string> script() const
    {
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const bool afterDot = i > 0 && tokens[i - 1].is(Token::Punct, ".");
            const bool lineStart = i == 0 || tokens[i - 1].kind == Token::Newline;
            if (tokens[i].is(Token::Punct, ".") && lineStart && i + 1 < tokens.size() && tokens[i + 1].is(Token::Word, "import")) {
                if (auto refused = qmlImport(i + 2))
                    return refused;
                continue;
            }
            if (afterDot)
                continue;
            if (tokens[i].is(Token::Word, "import") && i + 1 < tokens.size()) {
                const Token &next = tokens[i + 1];
                if (next.kind == Token::String) {
                    if (auto refused = path(next.text))
                        return refused;
                } else if (next.is(Token::Punct, "(")) {
                    if (i + 2 < tokens.size() && tokens[i + 2].kind == Token::String)
                        if (auto refused = path(tokens[i + 2].text))
                            return refused;
                } else if (auto refused = fromClause(i + 1)) {
                    return refused;
                }
            } else if (tokens[i].is(Token::Word, "export") && i + 1 < tokens.size() &&
                       (tokens[i + 1].is(Token::Punct, "*") || tokens[i + 1].is(Token::Punct, "{"))) {
                if (auto refused = fromClause(i + 1))
                    return refused;
            }
        }
        return std::nullopt;
    }
};

}

bool isQmlSource(std::string_view path)
{
    return path.ends_with(".qml") || path.ends_with(".js") || path.ends_with(".mjs");
}

std::optional<std::string> qmlImportProblem(std::string_view path, std::string_view text)
{
    const std::vector<Token> tokens = tokenise(text);
    const Imports imports{path, tokens};
    return path.ends_with(".qml") ? imports.qml() : imports.script();
}

}
