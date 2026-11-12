#include "plugin_state.h"

#include "hikari/api/api.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
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
    std::string lockRefusal;
    std::vector<std::string> sequence;
    std::map<std::string, std::function<std::expected<Json, Json>(const Json &)>> operations;

    std::expected<Json, Json> call(const std::string &operation, const Json &params) override
    {
        calls.emplace_back(operation, params);
        sequence.push_back(operation);
        if (const auto it = operations.find(operation); it != operations.end())
            return it->second(params);
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
    std::expected<void, std::string> lock() override
    {
        sequence.push_back("lock");
        if (!lockRefusal.empty())
            return std::unexpected(lockRefusal);
        return {};
    }
    void unlock() override { sequence.push_back("unlock"); }
    std::vector<std::pair<std::string, Json>> uiCalls;
    std::vector<Area> areas;
    std::map<std::string, std::function<std::expected<Json, Json>(const Json &)>> uiOperations;
    std::expected<Json, Json> request(Area area, const std::string &operation, const Json &params) override
    {
        uiCalls.emplace_back(operation, params);
        areas.push_back(area);
        if (const auto it = uiOperations.find(operation); it != uiOperations.end())
            return it->second(params);
        return Json::object();
    }
    std::vector<Json> paramsOf(const std::string &operation) const
    {
        std::vector<Json> out;
        for (const auto &[name, params] : calls)
            if (name == operation)
                out.push_back(params);
        return out;
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
    void serveDocument(std::size_t lineCount, std::int64_t &revision)
    {
        auto &ops = m_host.operations;
        ops["documents.get"] = [&revision](const Json &) {
            return Json::object({{"document", 4}, {"revision", revision}, {"title", "Episode 1"}, {"format", "ass"}});
        };
        ops["lines.list"] = [&revision, lineCount](const Json &params) {
            const auto offset = std::size_t(params.find("offset")->asInt());
            const auto limit = std::size_t(params.find("limit")->asInt());
            Json lines = Json::array();
            for (std::size_t i = offset; i < lineCount && i < offset + limit; ++i)
                lines.push(Json::object({{"id", std::int64_t(100 + i)}, {"row", std::int64_t(i + 1)},
                                         {"text", "line " + std::to_string(i + 1)}}));
            return Json::object({{"revision", revision}, {"total", std::int64_t(lineCount)}, {"lines", lines}});
        };
        ops["styles.list"] = [&revision](const Json &) {
            return Json::object({{"revision", revision}, {"styles", Json::array({Json::object({{"name", "Default"}})})}});
        };
        ops["info.list"] = [&revision](const Json &) {
            return Json::object(
                {{"revision", revision}, {"info", Json::array({Json::object({{"key", "Title"}, {"value", "Demo"}})})}});
        };
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

TEST_F(PluginStateTest, ASettingChangedInThePluginManagerReadsAtOnce)
{
    auto state = load("hikari.commands.register('greet', function() hikari.notify(tostring(hikari.settings.get('count'))) end)");
    ASSERT_TRUE(state) << state.error();
    ASSERT_TRUE((*state)->dispatch("settings:changed", Json::object({{"id", "count"}, {"value", 2}}), m_host));
    ASSERT_TRUE((*state)->dispatch("settings:changed", Json::object({{"id", "other"}, {"value", 9}}), m_host));
    ASSERT_TRUE((*state)->run("greet", m_host));
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"2"});
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
              "com.example.test 1.0.0 " + std::to_string(hikari::api::kApiVersion));
    EXPECT_EQ(evaluate("hikari.i18n.locale"), "en");
}

TEST_F(PluginStateTest, ADocumentReadPagesItsLinesAtOneRevision)
{
    std::int64_t revision = 7;
    serveDocument(6001, revision);
    auto state = load("local doc = hikari.document.read() "
                      "hikari.notify(table.concat({doc.document, doc.revision, doc.title, doc.format, #doc.lines, "
                      "doc.lines[6001].id, doc.lines[6001].text, doc.styles[1].name, doc.info[1].value}, ' '))");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"4 7 Episode 1 ass 6001 6100 line 6001 Default Demo"});
    const auto pages = m_host.paramsOf("lines.list");
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0].dump(), Json::parse(R"({"document": 4, "limit": 5000, "offset": 0})")->dump());
    EXPECT_EQ(pages[1].find("offset")->asInt(), 5000);
    EXPECT_EQ(m_host.paramsOf("documents.get").front().dump(), Json::object().dump());
}

TEST_F(PluginStateTest, AReadStartsAgainWhenTheDocumentMovesAndGivesUpAsBusy)
{
    std::int64_t revision = 1;
    serveDocument(3, revision);
    auto moving = m_host.operations["lines.list"];
    int moves = 1;
    m_host.operations["lines.list"] = [&](const Json &params) {
        auto page = moving(params);
        if (moves-- > 0)
            ++revision;
        return page;
    };
    auto state = load("local doc = hikari.document.read(4) hikari.notify(doc.revision .. ' ' .. #doc.lines)");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"2 3"});
    EXPECT_EQ(m_host.paramsOf("documents.get").size(), 2u);
    EXPECT_EQ(m_host.paramsOf("documents.get").front().dump(), Json::parse(R"({"document": 4})")->dump());

    m_host.calls.clear();
    moves = 100;
    auto busy = load("local doc, err = hikari.document.read() hikari.notify(tostring(doc) .. ' ' .. err.code)");
    ASSERT_TRUE(busy) << busy.error();
    EXPECT_EQ(m_host.texts().back(), "nil busy");
    EXPECT_EQ(m_host.paramsOf("documents.get").size(), 3u);
}

TEST_F(PluginStateTest, AnEditStagesEveryChangeIntoOneApplyUnderTheLock)
{
    std::int64_t revision = 9;
    serveDocument(2, revision);
    m_host.operations["document.apply"] = [](const Json &) {
        return Json::object({{"revision", 10}, {"inserted", Json::array({300})}, {"changed", true}});
    };
    auto state = load(R"~(
        local done = hikari.document.edit('Tidy', function(doc, change)
            local first, second = doc.lines[1].id, doc.lines[2].id
            change:set_line(first, {text = 'one'})
            change:insert_line({text = 'new'}, {after = first})
            change:delete_line(second)
            change:move_line(first)
            change:move_line(first, second)
            change:set_style('Default', {fontsize = 20})
            change:delete_style('Old')
            change:set_info('Title', 'Demo')
            change:set_info('Comment', nil)
        end)
        hikari.notify(done.revision .. ' ' .. done.inserted[1])
    )~");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"10 300"});
    const auto applied = m_host.paramsOf("document.apply");
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied[0].dump(), Json::parse(R"~({"document": 4, "edits": [
        {"fields": {"text": "one"}, "line": 100, "op": "set_line"},
        {"after": 100, "fields": {"text": "new"}, "op": "insert_line"},
        {"line": 101, "op": "delete_line"},
        {"before": null, "line": 100, "op": "move_line"},
        {"before": 101, "line": 100, "op": "move_line"},
        {"fields": {"fontsize": 20}, "name": "Default", "op": "set_style"},
        {"name": "Old", "op": "delete_style"},
        {"key": "Title", "op": "set_info", "value": "Demo"},
        {"key": "Comment", "op": "set_info", "value": null}],
        "name": "Tidy", "revision": 9})~")->dump());
    EXPECT_EQ(m_host.sequence.front(), "lock");
    EXPECT_EQ(m_host.sequence.at(m_host.sequence.size() - 2), "document.apply");
    EXPECT_EQ(m_host.sequence.back(), "unlock");
}

TEST_F(PluginStateTest, AnEditWithNothingStagedAppliesNothing)
{
    std::int64_t revision = 3;
    serveDocument(1, revision);
    auto state = load("local done = hikari.document.edit('Nothing', function() end) "
                      "hikari.notify(done.revision .. ' ' .. tostring(done.changed) .. ' ' .. #done.inserted)");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"3 false 0"});
    EXPECT_TRUE(m_host.paramsOf("document.apply").empty());
    EXPECT_EQ(m_host.sequence.back(), "unlock");
}

TEST_F(PluginStateTest, AStaleOrBusyEditReturnsTheError)
{
    std::int64_t revision = 3;
    serveDocument(1, revision);
    m_host.operations["document.apply"] = [](const Json &) -> std::expected<Json, Json> {
        return std::unexpected(Json::object({{"code", "stale_revision"}, {"message", "the Document changed"}}));
    };
    const std::string edit = "local done, err = hikari.document.edit('Fix', function(doc, change) "
                             "change:set_line(doc.lines[1].id, {text = 'x'}) end) "
                             "hikari.notify(tostring(done) .. ' ' .. err.code .. ': ' .. err.message)";
    auto stale = load(edit);
    ASSERT_TRUE(stale) << stale.error();
    EXPECT_EQ(m_host.texts().back(), "nil stale_revision: the Document changed");
    EXPECT_EQ(m_host.sequence.back(), "unlock");

    m_host.sequence.clear();
    m_host.lockRefusal = "another plugin is applying edits";
    auto busy = load(edit);
    ASSERT_TRUE(busy) << busy.error();
    EXPECT_EQ(m_host.texts().back(), "nil busy: another plugin is applying edits");
    EXPECT_EQ(m_host.sequence, std::vector<std::string>{"lock"});
}

TEST_F(PluginStateTest, AnEditUnlocksWhenItsFunctionFailsAndRefusesNesting)
{
    std::int64_t revision = 3;
    serveDocument(1, revision);
    auto failing = load("hikari.document.edit('Fix', function() error('no fix today') end)");
    ASSERT_FALSE(failing);
    EXPECT_NE(failing.error().find("main.lua:1: no fix today"), std::string::npos) << failing.error();
    EXPECT_EQ(m_host.sequence.back(), "unlock");

    m_host.sequence.clear();
    auto nested = load(R"~(
        local ok, err = pcall(hikari.document.edit, 'Outer', function()
            hikari.document.edit('Inner', function() end)
        end)
        hikari.document.edit('Again', function() end)
        hikari.notify(tostring(ok) .. ' ' .. err)
    )~");
    ASSERT_TRUE(nested) << nested.error();
    EXPECT_EQ(m_host.texts().back(), "false main.lua:3: hikari.document.edit can't run inside another edit");
    EXPECT_EQ(std::ranges::count(m_host.sequence, "lock"), 2);
    EXPECT_EQ(std::ranges::count(m_host.sequence, "unlock"), 2);
}

TEST_F(PluginStateTest, SelectionsGoThroughTheApi)
{
    auto state = load("hikari.selection.get() hikari.selection.get(5) "
                      "hikari.selection.set({101, 102}) hikari.selection.set({}, 103, 9) "
                      "hikari.selection.set({101}, hikari.null)");
    ASSERT_TRUE(state) << state.error();
    const auto gets = m_host.paramsOf("selection.get");
    ASSERT_EQ(gets.size(), 2u);
    EXPECT_EQ(gets[0].dump(), Json::object().dump());
    EXPECT_EQ(gets[1].dump(), Json::parse(R"({"document": 5})")->dump());
    const auto sets = m_host.paramsOf("selection.set");
    ASSERT_EQ(sets.size(), 3u);
    EXPECT_EQ(sets[0].dump(), Json::parse(R"({"selected": [101, 102]})")->dump());
    EXPECT_EQ(sets[1].dump(), Json::parse(R"({"active": 103, "document": 9, "selected": []})")->dump());
    EXPECT_EQ(sets[2].dump(), Json::parse(R"({"active": null, "selected": [101]})")->dump());
}

TEST_F(PluginStateTest, PanelsAndDialogsGoToHikariSubAsTheirOwnService)
{
    auto state = load(R"~(
        hikari.panels.render('board', {kind = 'label', text = 'Hi'})
        hikari.panels.show('board')
        hikari.dialogs.show('join', {title = 'Join', body = {kind = 'empty', text = 'Nothing'}})
        hikari.dialogs.close('join')
    )~");
    ASSERT_TRUE(state) << state.error();
    EXPECT_TRUE(m_host.calls.empty());
    ASSERT_EQ(m_host.uiCalls.size(), 4u);
    EXPECT_EQ(m_host.uiCalls[0].first, "panels.render");
    EXPECT_EQ(m_host.uiCalls[0].second.dump(), Json::parse(R"({"panel": "board", "tree": {"kind": "label", "text": "Hi"}})")->dump());
    EXPECT_EQ(m_host.uiCalls[1].second.dump(), Json::parse(R"({"panel": "board"})")->dump());
    EXPECT_EQ(m_host.uiCalls[2].first, "dialogs.show");
    EXPECT_EQ(m_host.uiCalls[3].first, "dialogs.close");
}

TEST_F(PluginStateTest, FilesAndNetworkGoToHikariSubAsTheirOwnServices)
{
    m_host.uiOperations["files.pick"] = [](const Json &params) -> std::expected<Json, Json> {
        if (params.find("save"))
            return Json::object();
        return *Json::parse(R"({"file": "f1", "name": "a.ass", "access": "read"})");
    };
    m_host.uiOperations["files.open"] = [](const Json &params) -> std::expected<Json, Json> {
        if (params.find("place")->asString() == "home")
            return std::unexpected(Json::object({{"code", "not_granted"}, {"message", "no place home"}}));
        return Json::object({{"file", "f2"}, {"name", params.find("path")->asString()}, {"access", "write"}});
    };
    m_host.uiOperations["files.folder"] = [](const Json &) { return *Json::parse(R"({"granted": false})"); };
    m_host.uiOperations["files.entries"] = [](const Json &) { return *Json::parse(R"({"entries": [{"name": "a", "folder": true}]})"); };
    m_host.uiOperations["net.fetch"] = [](const Json &) { return *Json::parse(R"({"status": 200, "body": "ok"})"); };
    m_host.operations["files.read"] = [](const Json &params) { return Json::object({{"data", params.find("file")->asString()}}); };
    auto state = load(R"~(
        local picked = hikari.files.pick({title = 'Pick'})
        hikari.notify(picked.file .. ' ' .. picked.name)
        hikari.notify(tostring(hikari.files.pick({save = true})))
        local note = hikari.files.open('data', 'notes/a.txt')
        hikari.notify(note.file .. ' ' .. note.name .. ' ' .. hikari.files.read(note))
        hikari.notify(tostring(hikari.files.write('f2', 'hi')))
        local missing, err = hikari.files.open('home', '.bashrc')
        hikari.notify(tostring(missing) .. ' ' .. err.code)
        hikari.notify(tostring(hikari.files.folder('exports')))
        hikari.notify(hikari.files.entries('cache')[1].name)
        hikari.notify(hikari.net.fetch('https://example.com/').body)
    )~");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), (std::vector<std::string>{"f1 a.ass", "nil", "f2 notes/a.txt f2", "true", "nil not_granted", "false", "a", "ok"}));
    EXPECT_EQ(m_host.areas, (std::vector<Area>{Area::Files, Area::Files, Area::Files, Area::Files, Area::Files, Area::Files, Area::Net}));
    EXPECT_EQ(m_host.uiCalls[5].second.dump(), Json::parse(R"({"path": "", "place": "cache"})")->dump());
    EXPECT_EQ(m_host.uiCalls[6].second.dump(), Json::parse(R"({"url": "https://example.com/"})")->dump());
    EXPECT_EQ(m_host.paramsOf("files.write").at(0).dump(), Json::parse(R"({"data": "hi", "file": "f2"})")->dump());

    auto unnamed = load("hikari.files.open(nil, 'a')");
    ASSERT_FALSE(unnamed);
    EXPECT_NE(unnamed.error().find("main.lua:1: hikari.files.open needs a place"), std::string::npos) << unnamed.error();
    auto noUrl = load("hikari.net.fetch({})");
    ASSERT_FALSE(noUrl);
    EXPECT_NE(noUrl.error().find("main.lua:1: hikari.net.fetch needs a url"), std::string::npos) << noUrl.error();
}

TEST_F(PluginStateTest, AskReturnsTheButtonAndTheValues)
{
    m_host.uiOperations["dialogs.ask"] = [](const Json &) {
        return *Json::parse(R"({"button": "join", "values": {"glue": "space"}})");
    };
    auto state = load(R"~(
        local button, values = hikari.dialogs.ask('join', {title = 'Join'})
        hikari.notify(button .. ' ' .. values.glue)
    )~");
    ASSERT_TRUE(state) << state.error();
    EXPECT_EQ(m_host.texts(), std::vector<std::string>{"join space"});
}

TEST_F(PluginStateTest, ARefusedRenderRaisesAtTheCallersLine)
{
    m_host.uiOperations["panels.render"] = [](const Json &) -> std::expected<Json, Json> {
        return std::unexpected(Json::object({{"code", "invalid_params"}, {"message", "tree: unknown kind \u201cfoo\u201d"}}));
    };
    auto state = load("\nhikari.panels.render('board', {kind = 'foo'})");
    ASSERT_FALSE(state);
    EXPECT_NE(state.error().find("main.lua:2: tree: unknown kind"), std::string::npos) << state.error();
    auto unnamed = load("hikari.panels.show('')");
    ASSERT_FALSE(unnamed);
    EXPECT_NE(unnamed.error().find("main.lua:1: hikari.panels.show needs an id"), std::string::npos) << unnamed.error();
}

TEST_F(PluginStateTest, PanelAndDialogHandlersRunWithoutAnApiSubscription)
{
    auto state = load("hikari.panels.on('board', function(e) hikari.notify('panel ' .. e.widget) end) "
                      "hikari.dialogs.on('join', function(e) hikari.notify('dialog ' .. e.button) end)");
    ASSERT_TRUE(state) << state.error();
    EXPECT_TRUE(m_host.subscriptions.empty());
    ASSERT_TRUE((*state)->dispatch("panel:board", Json::object({{"kind", "click"}, {"widget", "refresh"}}), m_host));
    ASSERT_TRUE((*state)->dispatch("dialog:join", Json::object({{"kind", "button"}, {"button", "cancel"}}), m_host));
    EXPECT_EQ(m_host.texts(), (std::vector<std::string>{"panel refresh", "dialog cancel"}));
}
