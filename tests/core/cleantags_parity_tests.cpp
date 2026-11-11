#include "hikari/core/tag_cleanup.h"

#include <gtest/gtest.h>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace tc = hikari::core::tagcleanup;

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

class CleanTagsLua {
public:
    CleanTagsLua() : m_lua(luaL_newstate(), lua_close)
    {
        lua_State *L = m_lua.get();
        luaL_openlibs(L);
        const char *stub = "aegisub = { progress = { is_cancelled = function() return false end } }\n"
                           "function tagmerge(t) repeat t,r=t:gsub(\"({\\\\[^}]-)}{(\\\\[^}]-})\",\"%1%2\") until r==0 return t end";
        if (luaL_dostring(L, stub) != 0)
            throw std::runtime_error(lua_tostring(L, -1));
        std::string source = readFile(HIKARI_CLEANTAGS_LUA);
        if (source.starts_with("\xEF\xBB\xBF"))
            source.erase(0, 3);
        if (luaL_loadbuffer(L, source.data(), source.size(), "cleantags.lua") != 0 || lua_pcall(L, 0, 0, 0) != 0)
            throw std::runtime_error(lua_tostring(L, -1));
    }

    std::string clean(const std::string &text) const { return call("cleantags", text); }
    std::string tagmerge(const std::string &text) const { return call("tagmerge", text); }

private:
    std::string call(const char *function, const std::string &text) const
    {
        lua_State *L = m_lua.get();
        lua_getglobal(L, function);
        lua_pushlstring(L, text.data(), text.size());
        if (lua_pcall(L, 1, 1, 0) != 0) {
            std::string error = lua_tostring(L, -1);
            lua_pop(L, 1);
            throw std::runtime_error(error);
        }
        std::size_t n = 0;
        const char *out = lua_tolstring(L, -1, &n);
        std::string result(out, n);
        lua_pop(L, 1);
        return result;
    }

    std::unique_ptr<lua_State, decltype(&lua_close)> m_lua;
};

template <typename Native>
std::string native(const std::string &text, Native function)
{
    const auto out = function(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
    return std::string(reinterpret_cast<const char *>(out.data()), out.size());
}

std::string native(const std::string &text)
{
    return native(text, tc::cleanTags);
}

std::vector<std::string> fixtureTexts()
{
    std::vector<std::string> texts;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(HIKARI_FIXTURES_DIR)) {
        if (entry.path().extension() != ".ass")
            continue;
        std::istringstream lines(readFile(entry.path()));
        for (std::string line; std::getline(lines, line);) {
            if (!line.starts_with("Dialogue:") && !line.starts_with("Comment:"))
                continue;
            std::size_t at = 0;
            for (int commas = 0; commas < 9 && at != std::string::npos; ++commas)
                at = line.find(',', at == 0 ? 0 : at + 1);
            if (at == std::string::npos)
                continue;
            std::string text = line.substr(at + 1);
            if (text.ends_with('\r'))
                text.pop_back();
            texts.push_back(std::move(text));
        }
    }
    return texts;
}

TEST(CleanTagsParity, HandWrittenCases)
{
    const CleanTagsLua lua;
    for (const std::string text : {
             "",
             "plain",
             "{\\b1}{\\i1}text",
             "{\\k10}{\\k20}a",
             "{\\k10}{\\k20}{\\k30}a{\\b1}{\\k5}b",
             "{\\b1\\k10\\i1\\kf20\\u1}x",
             "a{\\an8}b{\\an2}c{\\pos(1,2)}d{\\move(1,2,3,4)}",
             "{\\fad(10,20)}{\\fade(1,2,3,4,5,6,7)}x",
             "{\\pos( 10 , 20 )\\clip( 1, 2, 3, 4)}x",
             "{\\iclip(m 0 0 l 10 10)\\clip(1 ,2)}x",
             "{\\t( 0 , 100 , \\fs20 )}x",
             "{ }{}x{  }",
             "{comment}{\\b1}text{\\org(1,2)}",
             "\x01{\\b1}\x02text",
             "{\\an8\\a5}{\\org(3,4)\\org(5,6)}t",
             "{unclosed\\b1 text",
             "text}{\\b1}",
             "日本{\\i1}語{\\i0}",
         })
        EXPECT_EQ(native(text), lua.clean(text)) << text;
}

TEST(CleanTagsParity, FixtureLines)
{
    const CleanTagsLua lua;
    const auto texts = fixtureTexts();
    ASSERT_GT(texts.size(), 100u);
    for (const auto &text : texts)
        ASSERT_EQ(native(text), lua.clean(text)) << text;
}

TEST(CleanTagsParity, GeneratedLines)
{
    const CleanTagsLua lua;
    const std::vector<std::string> tokens{
        "{", "}", "{", "}", "\\k10", "\\kf5", "\\ko3", "\\K7", "\\b1", "\\i0", "\\an8", "\\a5", "\\an", "\\pos(1,2)",
        "\\pos( 1 , 2 )", "\\move(1,2,3,4)", "\\move(1,2,3,4,5,6)", "\\org(3,4)", "\\fad(10,20)",
        "\\fade(1,2,3,4,5,6,7)", "\\clip(1, 2, 3, 4)", "\\iclip(m 0 0 l 1 1)", "\\clip( 1,2)", "\\t(0,100,\\bord2)",
        "\\t( \\fs20 )", " ", "  ", "\t", "text", "日本", "\\N", "\x01", "\x02", "\\fnArial Bold", "(", ")", ",",
        "\\1c&HFF&", "\\", "%", "comment"};
    std::mt19937 random(839);
    std::uniform_int_distribution<std::size_t> pick(0, tokens.size() - 1);
    std::uniform_int_distribution<int> length(0, 24);
    for (int n = 0; n < 20000; ++n) {
        std::string text;
        for (int i = length(random); i > 0; --i)
            text += tokens[pick(random)];
        ASSERT_EQ(native(text), lua.clean(text)) << text;
        ASSERT_EQ(native(text, tc::mergeBlocks), lua.tagmerge(text)) << text;
    }
}

}
