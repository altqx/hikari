#include "plugin_state.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <random>

using hikari::api::Json;
using namespace hikari::plugin_helper;

namespace {

struct Notice {
    Level level;
    bool toast;
    std::string text;
};

class FakeHost : public Host {
public:
    std::vector<Notice> notices;
    std::vector<std::pair<std::string, Json>> calls;
    std::vector<std::pair<std::string, Json>> settings;
    std::vector<std::string> subscriptions;
    std::string settingRefusal;

    std::expected<Json, Json> call(const std::string &operation, const Json &params) override
    {
        calls.emplace_back(operation, params);
        if (operation == "missing.op")
            return std::unexpected(Json::object({{"code", "unknown_operation"}, {"message", "no such operation"}}));
        return Json::object({{"echo", params}, {"nothing", nullptr}, {"list", Json::array({1, 2})}});
    }
    void notify(Level level, bool toast, const std::string &text) override { notices.push_back({level, toast, text}); }
    std::expected<void, std::string> setSetting(const std::string &id, const Json &value) override
    {
        if (!settingRefusal.empty())
            return std::unexpected(settingRefusal);
        settings.emplace_back(id, value);
        return {};
    }
    std::expected<void, std::string> subscribe(const std::string &event) override
    {
        if (event == "no.such.event")
            return std::unexpected("there is no event “no.such.event”");
        subscriptions.push_back(event);
        return {};
    }
    std::vector<std::string> texts() const
    {
        std::vector<std::string> out;
        for (const auto &n : notices)
            out.push_back(n.text);
        return out;
    }
};

std::string bytecodeLiteral(const std::string &source)
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    luaL_loadstring(L, source.c_str());
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "dump");
    lua_pushvalue(L, -3);
    lua_call(L, 1, 1);
    std::size_t size = 0;
    const char *data = lua_tolstring(L, -1, &size);
    std::string literal = "\"";
    for (std::size_t i = 0; i < size; ++i)
        literal += "\\" + std::to_string(static_cast<unsigned char>(data[i]));
    lua_close(L);
    return literal + "\"";
}

}

class PluginStateTest : public ::testing::Test {
protected:
    std::filesystem::path m_root;
    FakeHost m_host;

    void SetUp() override
    {
        std::random_device seed;
        m_root = std::filesystem::temp_directory_path() / ("hikari-plugin-state-" + std::to_string(seed()));
        std::filesystem::create_directories(m_root);
    }
    void TearDown() override { std::filesystem::remove_all(m_root); }

    void write(const std::string &path, const std::string &text)
    {
        const auto full = m_root / path;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream(full, std::ios::binary) << text;
    }
    Options options(Tier tier = Tier::Restricted)
    {
        Options o;
        o.root = m_root;
        o.main = "main.lua";
        o.id = "com.example.test";
        o.version = "1.0.0";
        o.tier = tier;
        o.memoryLimit = 32u << 20;
        o.settings = Json::object({{"greeting", "hello"}, {"count", 3}});
        o.commands = {"greet", "count"};
        return o;
    }
    std::expected<std::unique_ptr<PluginState>, std::string> load(const std::string &main, Options o)
    {
        write("main.lua", main);
        return PluginState::load(std::move(o), m_host);
    }
    std::expected<std::unique_ptr<PluginState>, std::string> load(const std::string &main)
    {
        return load(main, options());
    }
    // The text the main file's last hikari.notify gave.
    std::string evaluate(const std::string &expression, Tier tier = Tier::Restricted)
    {
        m_host.notices.clear();
        auto state = load("hikari.notify(tostring(" + expression + "))", options(tier));
        if (!state)
            return "error: " + state.error();
        return m_host.notices.empty() ? std::string() : m_host.notices.back().text;
    }
};

TEST_F(PluginStateTest, ARestrictedPluginReachesNoUnsafeLibrary)
{
    for (const char *name : {"ffi", "io", "os", "debug", "jit", "package", "dofile", "loadfile", "module",
                             "string.dump", "(\"\").dump"})
        EXPECT_EQ(evaluate(name), "nil") << name;
    for (const char *name : {"string", "table", "math", "bit", "coroutine", "load", "loadstring", "require", "pcall"})
        EXPECT_NE(evaluate(name), "nil") << name;
}

TEST_F(PluginStateTest, RequireNeverReachesABuiltInLibrary)
{
    for (const char *name : {"ffi", "io", "os", "debug", "jit", "jit.util", "package", "string.buffer"}) {
        auto state = load(std::string("require '") + name + "'");
        ASSERT_FALSE(state) << name;
        EXPECT_NE(state.error().find("isn't in the plugin's lua folder"), std::string::npos) << state.error();
    }
}

TEST_F(PluginStateTest, NoBytecodeLoadsInTheRestrictedTier)
{
    const std::string bytecode = bytecodeLiteral("return 42");
    for (const std::string &loader : {"load(" + bytecode + ")", "loadstring(" + bytecode + ")",
                                      "load(function() local s = " + bytecode + "; local done; "
                                      "return (function() if done then return nil end done = true return s end)() end)",
                                      "load(" + bytecode + ", 'x', 'b')"}) {
        const std::string result = evaluate("select(2, " + loader + ")");
        EXPECT_NE(result.find("wrong mode"), std::string::npos) << loader << " -> " << result;
    }
    EXPECT_EQ(evaluate("load('return 6 * 7')()"), "42");
}

TEST_F(PluginStateTest, ABytecodeFileIsRefused)
{
    lua_State *L = luaL_newstate();
    luaL_loadstring(L, "return 1");
    std::string chunk;
    lua_dump(L, [](lua_State *, const void *p, std::size_t sz, void *ud) {
        static_cast<std::string *>(ud)->append(static_cast<const char *>(p), sz);
        return 0;
    }, &chunk);
    lua_close(L);
    write("lua/blob.lua", chunk);
    auto viaRequire = load("require 'blob'");
    ASSERT_FALSE(viaRequire);
    EXPECT_NE(viaRequire.error().find("lua/blob.lua is compiled bytecode"), std::string::npos) << viaRequire.error();
    auto asMain = load(chunk);
    ASSERT_FALSE(asMain);
    EXPECT_EQ(asMain.error(), "main.lua is compiled bytecode; plugins load Lua source only");
}

TEST_F(PluginStateTest, RequireReadsOnlyThePackagesLuaFolder)
{
    write("lua/tools/text.lua", "loads = (loads or 0) + 1; return {shout = function(s) return s:upper() end}");
    write("lua/widgets/init.lua", "return 'widgets'");
    write("outside.lua", "return 'outside'");
    auto state = load("local t = require 'tools.text'; require 'tools.text'; "
                      "hikari.notify(t.shout('hi') .. ' ' .. loads .. ' ' .. require 'widgets')");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"HI 1 widgets"});
    for (const char *name : {"../outside", "..outside", "/etc/passwd", "tools/text", "tools..text", ".tools", "C:"}) {
        auto escaped = load(std::string("require '") + name + "'");
        ASSERT_FALSE(escaped) << name;
        EXPECT_NE(escaped.error().find("isn't a module name"), std::string::npos) << escaped.error();
    }
}

TEST_F(PluginStateTest, AModuleThatRequiresItselfIsReported)
{
    write("lua/loop.lua", "require 'loop'");
    auto state = load("require 'loop'");
    ASSERT_FALSE(state);
    EXPECT_NE(state.error().find("requires itself"), std::string::npos) << state.error();
}

TEST_F(PluginStateTest, TheMemoryLimitStopsAGreedyPlugin)
{
    Options o = options();
    o.memoryLimit = 8u << 20;
    auto state = load("local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 100) .. i end", o);
    ASSERT_FALSE(state);
    EXPECT_EQ(state.error(), "the plugin ran out of memory: it may hold 8 MB");
}

TEST_F(PluginStateTest, AMemoryErrorInACommandLeavesThePluginUsable)
{
    Options o = options();
    o.memoryLimit = 8u << 20;
    auto state = load("hikari.commands.register('greet', function() local t = {} "
                      "for i = 1, 1e7 do t[i] = string.rep('x', 100) .. i end end) "
                      "hikari.commands.register('count', function() hikari.notify('still here') end)", o);
    ASSERT_TRUE(state) << state.error();
    EXPECT_FALSE((*state)->run("greet", m_host));
    EXPECT_TRUE((*state)->run("count", m_host));
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"still here"});
    EXPECT_LT((*state)->memoryInUse(), o.memoryLimit);
}

TEST_F(PluginStateTest, AnUnrestrictedPluginHasTheFullLibraries)
{
    for (const char *name : {"io ~= nil", "os ~= nil", "debug ~= nil", "jit ~= nil",
                             "package.loadlib ~= nil", "require('ffi') ~= nil"})
        EXPECT_EQ(evaluate(name, Tier::Unrestricted), "true") << name;
    write("lua/helper.lua", "return 'from lua'");
    EXPECT_EQ(evaluate("require 'helper'", Tier::Unrestricted), "from lua");
}

TEST_F(PluginStateTest, CommandsRunTheirRegisteredHandlers)
{
    auto state = load("hikari.commands.register('greet', function() hikari.notify(hikari.settings.get('greeting')) end)");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ((*state)->handledCommands(), std::vector<std::string>{"greet"});
    ASSERT_TRUE((*state)->run("greet", m_host));
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"hello"});
    EXPECT_TRUE(m_host.notices.back().toast);
    auto unhandled = (*state)->run("count", m_host);
    ASSERT_FALSE(unhandled);
    EXPECT_NE(unhandled.error().find("hikari.commands.register"), std::string::npos);
}

TEST_F(PluginStateTest, OnlyDeclaredCommandsTakeHandlersAndOnlyOnce)
{
    auto undeclared = load("hikari.commands.register('secret', function() end)");
    ASSERT_FALSE(undeclared);
    EXPECT_NE(undeclared.error().find("“secret” isn't a command in hikari-plugin.json"), std::string::npos);
    auto twice = load("hikari.commands.register('greet', print) hikari.commands.register('greet', print)");
    ASSERT_FALSE(twice);
    EXPECT_NE(twice.error().find("already has a handler"), std::string::npos);
}

TEST_F(PluginStateTest, AnErrorCarriesItsPlaceAndTraceback)
{
    write("lua/broken.lua", "return {go = function() error('broke here') end}");
    auto state = load("hikari.commands.register('greet', function() require('broken').go() end)");
    ASSERT_TRUE(state) << state.error();
    auto ran = (*state)->run("greet", m_host);
    ASSERT_FALSE(ran);
    EXPECT_NE(ran.error().find("lua/broken.lua:1: broke here"), std::string::npos) << ran.error();
    EXPECT_NE(ran.error().find("stack traceback"), std::string::npos) << ran.error();
    EXPECT_EQ(ran.error().find(m_root.string()), std::string::npos) << ran.error();
}

TEST_F(PluginStateTest, SettingsReadAndWriteThroughHikariSub)
{
    auto state = load("hikari.commands.register('count', function() "
                      "hikari.settings.set('count', hikari.settings.get('count') + 1) "
                      "hikari.notify(tostring(hikari.settings.get('count'))) end)");
    ASSERT_TRUE(state) << state.error();
    ASSERT_TRUE((*state)->run("count", m_host));
    ASSERT_EQ(m_host.settings.size(), 1u);
    EXPECT_EQ(m_host.settings[0].first, "count");
    EXPECT_EQ(m_host.settings[0].second, Json(4));
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"4"});

    m_host.settingRefusal = "“count” must be at most 5";
    m_host.notices.clear();
    auto refused = (*state)->run("count", m_host);
    ASSERT_FALSE(refused);
    EXPECT_NE(refused.error().find("“count” must be at most 5"), std::string::npos);
    EXPECT_EQ(evaluate("pcall(hikari.settings.get, 'other')"), "false");
}

TEST_F(PluginStateTest, CallsReachTheHikariApiAsJson)
{
    auto state = load("local r = hikari.call('lines.list', {fields = hikari.array({'text'}), limit = 2, "
                      "nested = {a = true, n = hikari.null}}) "
                      "hikari.notify(tostring(r.nothing == hikari.null) .. ' ' .. #r.list .. ' ' .. r.echo.limit) "
                      "local none, err = hikari.call('missing.op') "
                      "hikari.notify(tostring(none) .. ' ' .. err.code) "
                      "hikari.notify(tostring(select(2, pcall(hikari.call, 'x', {1, nil, 3}))))"
                      "hikari.notify(tostring(select(2, pcall(hikari.call, 'x', {1, a = 2}))))"
                      "hikari.commands.run('GLOBAL_SAVE_SUBS')");
    ASSERT_TRUE(state) << state.error();
    ASSERT_GE(m_host.calls.size(), 3u);
    EXPECT_EQ(m_host.calls[0].first, "lines.list");
    EXPECT_EQ(m_host.calls[0].second.dump(),
              R"({"fields":["text"],"limit":2,"nested":{"a":true,"n":null}})");
    EXPECT_EQ(m_host.calls[1].second, Json::object());
    EXPECT_EQ(m_host.calls.back().first, "commands.run");
    EXPECT_EQ(m_host.calls.back().second.dump(), R"({"command":"GLOBAL_SAVE_SUBS"})");
    const auto texts = m_host.texts();
    ASSERT_EQ(texts.size(), 4u);
    EXPECT_EQ(texts[0], "true 2 2");
    EXPECT_EQ(texts[1], "nil unknown_operation");
    EXPECT_NE(texts[2].find("holes"), std::string::npos) << texts[2];
    EXPECT_NE(texts[3].find("mixes"), std::string::npos) << texts[3];
}

TEST_F(PluginStateTest, EventsSubscribeOnceAndEveryHandlerRuns)
{
    auto state = load("hikari.events.on('document.changed', function(e) hikari.notify('a ' .. e.document) end) "
                      "hikari.events.on('document.changed', function(e) error('b fails') end) "
                      "hikari.events.on('document.changed', function(e) hikari.notify('c ' .. e.revision) end)");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.subscriptions, std::vector<std::string>{"document.changed"});
    auto dispatched = (*state)->dispatch("document.changed", Json::object({{"document", "d1"}, {"revision", 7}}), m_host);
    ASSERT_FALSE(dispatched);
    EXPECT_NE(dispatched.error().find("b fails"), std::string::npos);
    EXPECT_EQ(m_host.texts(), (std::vector<std::string>{"a d1", "c 7"}));
    EXPECT_TRUE((*state)->dispatch("selection.changed", Json::object(), m_host));
    auto unknown = load("hikari.events.on('no.such.event', print)");
    ASSERT_FALSE(unknown);
    EXPECT_NE(unknown.error().find("there is no event"), std::string::npos);
}

TEST_F(PluginStateTest, PrintGoesToTheLogWithoutAToast)
{
    ASSERT_TRUE(load("print('one', 2, nil) hikari.notify('careful', 'warning')"));
    ASSERT_EQ(m_host.notices.size(), 2u);
    EXPECT_EQ(m_host.notices[0].text, "one\t2\tnil");
    EXPECT_FALSE(m_host.notices[0].toast);
    EXPECT_EQ(m_host.notices[1].level, Level::Warning);
    EXPECT_TRUE(m_host.notices[1].toast);
}

TEST_F(PluginStateTest, TranslationsFallBackFromRegionToLanguageToEnglish)
{
    write("i18n/en.json", R"({"hello": "Hello, %1", "bye": "Bye", "only.en": "English"})");
    write("i18n/pt.json", R"({"hello": "Olá, %1", "bye": "Tchau"})");
    write("i18n/pt_BR.json", R"({"bye": "Falou"})");
    Options o = options();
    o.locale = "pt_BR";
    auto state = load("local t = hikari.i18n.t "
                      "hikari.notify(hikari.i18n.locale .. '|' .. t('hello', 'Ana') .. '|' .. t('bye') .. '|' .. "
                      "t('only.en') .. '|' .. t('missing.key') .. '|' .. t('100%1', 5))", o);
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"pt_BR|Olá, Ana|Falou|English|missing.key|1005"});
    write("i18n/en.json", R"(["not", "an", "object"])");
    auto broken = load("", options());
    ASSERT_FALSE(broken);
    EXPECT_EQ(broken.error(), "i18n/en.json must be a JSON object of strings");
}

TEST_F(PluginStateTest, TheHikariGlobalNamesThePlugin)
{
    EXPECT_EQ(evaluate("hikari.plugin.id .. ' ' .. hikari.plugin.version .. ' ' .. hikari.api_version"),
              "com.example.test 1.0.0 1");
    EXPECT_EQ(evaluate("hikari.i18n.locale"), "en");
}
