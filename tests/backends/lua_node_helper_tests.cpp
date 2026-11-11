#include "hikari/application/node_effects.h"
#include "hikari/application/node_graph.h"
#include "hikari/backends/lua_node_helper.h"
#include "hikari/backends/lua_protocol.h"
#include "hikari/core/ass_load.h"

#include <QCoreApplication>
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef Q_OS_LINUX
#include <csignal>
#include <ctime>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace hikari;
using namespace hikari::application;
using backends::LuaNodeHelper;

namespace {

class LuaNodeHelperTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "lua_node_helper_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
};

LuaNodeHelper::Options real()
{
    const QString helper = QStringLiteral(HIKARI_LUA_HELPER);
    return {helper, 10'000, backends::luaNodeProfile(helper)};
}

LuaNodeHelper::Options fake(int timeLimitMs = 10'000)
{
    const QString helper = QStringLiteral(HIKARI_FAKE_LUA_NODE_HELPER);
    return {helper, timeLimitMs, backends::luaNodeProfile(helper)};
}

LuaNodePort::Call callOf(std::string code, std::vector<PortSpec> inputs, std::vector<PortSpec> outputs,
                         std::vector<std::vector<NodeValue>> rows)
{
    return {"node", std::move(code), std::move(inputs), std::move(outputs), std::move(rows)};
}

const std::vector<PortSpec> kNumberIn{{"i", PortType::Number}};
const std::vector<PortSpec> kNumberOut{{"out", PortType::Number}};

std::vector<std::vector<NodeValue>> numbers(std::initializer_list<double> values)
{
    std::vector<std::vector<NodeValue>> rows;
    for (const double v : values)
        rows.push_back({v});
    return rows;
}

class FlatMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &, const std::string &text) override
    {
        return TextExtents{10.0 * static_cast<double>(text.size()), 20, 4, 0};
    }
};

core::Document documentWithGraph(std::string_view graph)
{
    std::ifstream in(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::string fixture = text.str();
    const auto parsed = parseGraph(graph);
    EXPECT_TRUE(parsed);
    fixture.insert(fixture.find("Dialogue: "),
                   "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,graph doubled," + graphText(*parsed) + "\n");
    std::vector<std::byte> bytes(fixture.size());
    std::memcpy(bytes.data(), fixture.data(), fixture.size());
    return core::loadAss(bytes).document;
}

std::string luaGraph(std::string_view code)
{
    return R"({"format": 1, "nodes": [
    {"id": "syl", "type": "units.for_each"},
    {"id": "info", "type": "units.info"},
    {"id": "double", "type": "lua.code", "fields": {"code": ")" +
           std::string(code) + R"(",
        "inputs": [{"name": "i", "type": "Number"}], "outputs": [{"name": "out", "type": "Number"}]}},
    {"id": "layer", "type": "output.emit"}],
  "wires": [{"from": "info.i", "to": "double.i"}, {"from": "syl.units", "to": "layer.units"},
            {"from": "double.out", "to": "layer.layer"}]})";
}

std::expected<NodeEffectRun, GraphProblem> runGraph(const core::Document &doc, LuaNodePort &lua, bool trusted)
{
    FlatMeasure measure;
    NodeEffectOptions options;
    options.measure = &measure;
    options.lua = &lua;
    options.trustLua = trusted;
    return runDocumentGraph(doc, documentGraphs(doc).front().line, options);
}

class Doubling : public LuaNodePort {
public:
    std::expected<std::vector<std::vector<NodeValue>>, Failure> run(const Call &call) override
    {
        std::vector<std::vector<NodeValue>> out;
        for (const auto &row : call.rows)
            out.push_back({std::get<double>(row[0]) * 2});
        return out;
    }
};

#ifdef Q_OS_LINUX
pid_t threadNamed(const std::string &name)
{
    for (const auto &task : std::filesystem::directory_iterator("/proc/self/task")) {
        std::ifstream comm(task.path() / "comm");
        std::string line;
        std::getline(comm, line);
        if (line == name.substr(0, 15))
            return static_cast<pid_t>(std::stoi(task.path().filename().string()));
    }
    return 0;
}

void sleepInTheHandler(int)
{
    const timespec stall{1, 500'000'000};
    nanosleep(&stall, nullptr);
}
#endif

std::vector<int> layers(const NodeEffectRun &run)
{
    std::vector<int> out;
    for (const core::LineRecord &line : run.lines)
        out.push_back(line.layer.value);
    return out;
}

}

TEST(LuaNodeProtocol, ACallAndItsRowsRoundTripEveryLuaType)
{
    const std::vector<PortSpec> ports{{"n", PortType::Number}, {"s", PortType::Text},   {"c", PortType::Colour},
                                      {"a", PortType::Alpha},  {"t", PortType::Time},   {"p", PortType::Point},
                                      {"b", PortType::Bool},   {"g", PortType::Tags}};
    const std::vector<NodeValue> row{1.5, std::string("syl"), NodeColour{1, 2, 3}, 128.0, 250.0,
                                     NodePoint{4, 5}, true, NodeTags{"\\an5"}};
    const LuaNodePort::Call call = callOf("return", ports, ports, {row, row});
    const auto payload = backends::lua::encodeNodeCall(call);
    backends::helper::Reader in(payload);
    const auto decoded = backends::lua::decodeNodeCall(in, payload.size());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->node, "node");
    EXPECT_EQ(decoded->code, "return");
    EXPECT_EQ(decoded->rows, call.rows);
    const auto rows = backends::lua::decodeNodeRows(backends::lua::encodeNodeRows(call.rows, ports), ports);
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, call.rows);
}

TEST(LuaNodeProtocol, APortWithoutALuaFormIsRefused)
{
    const auto payload = backends::lua::encodeNodeCall(callOf("", {{"d", PortType::Drawing}}, {}, {}));
    backends::helper::Reader in(payload);
    EXPECT_FALSE(backends::lua::decodeNodeCall(in, payload.size()));
}

TEST_F(LuaNodeHelperTest, EachRowRunsInFreshGlobalsWithOnlyTheNodeEnvironment)
{
    LuaNodeHelper lua(real());
    const std::string code =
        "out = io == nil and os == nil and require == nil and load == nil and loadstring == nil and "
        "dofile == nil and loadfile == nil and debug == nil and ffi == nil and jit == nil and aegisub == nil and "
        "getfenv == nil and setfenv == nil and collectgarbage == nil and package == nil and coroutine == nil and "
        "math.random == nil and math.randomseed == nil and string.dump == nil and getmetatable('') == false and "
        "leftover == nil and string.upper ~= nil and _G.i == i and bit.band(6, 3) == 2 and table.concat({'a', 'b'}) == 'ab' and "
        "('x'):rep(2) == 'xx' and math.floor(2.5) == 2 and type(pairs) == 'function'\n"
        "leftover = true\nstring.upper = nil";
    const auto result = lua.run(callOf(code, kNumberIn, {{"out", PortType::Bool}}, numbers({1, 2, 3})));
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(*result, (std::vector<std::vector<NodeValue>>{{true}, {true}, {true}}));
    EXPECT_EQ(lua.starts(), 1);
}

TEST_F(LuaNodeHelperTest, EveryLuaTypeReachesTheCodeAndComesBack)
{
    LuaNodeHelper lua(real());
    const std::vector<PortSpec> in{{"n", PortType::Number}, {"s", PortType::Text}, {"c", PortType::Colour},
                                   {"a", PortType::Alpha},  {"t", PortType::Time}, {"p", PortType::Point},
                                   {"b", PortType::Bool},   {"g", PortType::Tags}};
    std::vector<PortSpec> out;
    for (const PortSpec &p : in)
        out.push_back({"o" + p.name, p.type});
    const std::string code = "on = n + 1; oc = c:lower(); oa = a; ot = t * 2; op = {x = p.x + 1, y = p.y}; "
                             "ob = not b; og = g .. '\\\\bord2'; os = s .. '!'";
    const std::vector<NodeValue> row{1.5, std::string("syl"), NodeColour{10, 11, 255}, 128.0, 250.0,
                                     NodePoint{4, 5}, true, NodeTags{"\\an5"}};
    const auto result = lua.run(callOf(code, in, out, {row}));
    ASSERT_TRUE(result) << result.error().message;
    const std::vector<NodeValue> expected{2.5, std::string("syl!"), NodeColour{10, 11, 255}, 128.0, 500.0,
                                          NodePoint{5, 5}, false, NodeTags{"\\an5\\bord2"}};
    EXPECT_EQ(*result, (std::vector<std::vector<NodeValue>>{expected}));
}

TEST_F(LuaNodeHelperTest, CodeThatFailsSaysWhyAndKeepsTheHelper)
{
    LuaNodeHelper lua(real());
    const auto raised = lua.run(callOf("error('boom')", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(raised);
    EXPECT_EQ(raised.error().message, "code:1: boom");
    EXPECT_FALSE(raised.error().stopsRun);

    const auto wrong = lua.run(callOf("out = 'x'", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(wrong);
    EXPECT_EQ(wrong.error().message, "output \"out\" is a string, not a Number");

    const auto unset = lua.run(callOf("local out = 1", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(unset);
    EXPECT_EQ(unset.error().message, "did not set its output \"out\"");

    const auto bytecode = lua.run(callOf("\x1bLJ\x02", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(bytecode);
    EXPECT_FALSE(bytecode.error().stopsRun);

    const auto fine = lua.run(callOf("out = i * 2", kNumberIn, kNumberOut, numbers({1, 2})));
    ASSERT_TRUE(fine) << fine.error().message;
    EXPECT_EQ(*fine, numbers({2, 4}));
    EXPECT_EQ(lua.starts(), 1);
}

TEST_F(LuaNodeHelperTest, ABatchPastTheTimeLimitStopsTheHelperAndTheNextStartsANewOne)
{
    LuaNodeHelper lua(fake(300));
    const auto stuck = lua.run(callOf("hang", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(stuck);
    EXPECT_EQ(stuck.error().message, "took longer than 0.3 seconds and was stopped");
    EXPECT_TRUE(stuck.error().stopsRun);

    const auto next = lua.run(callOf("echo", kNumberIn, kNumberOut, numbers({7})));
    ASSERT_TRUE(next) << next.error().message;
    EXPECT_EQ(*next, numbers({7}));
    EXPECT_EQ(lua.starts(), 2);
}

TEST_F(LuaNodeHelperTest, ABatchThatAnsweredWhileItsThreadWasStalledIsNotPastTheTimeLimit)
{
#ifndef Q_OS_LINUX
    GTEST_SKIP() << "stalls the helper's thread with a Linux signal";
#else
    LuaNodeHelper lua(fake(600));
    ASSERT_TRUE(lua.run(callOf("echo", kNumberIn, kNumberOut, numbers({1}))));
    const pid_t worker = threadNamed("hikari.lua.nodes");
    ASSERT_GT(worker, 0);
    struct sigaction stall{};
    struct sigaction previous{};
    stall.sa_handler = sleepInTheHandler;
    sigaction(SIGUSR2, &stall, &previous);
    std::thread staller([worker] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        syscall(SYS_tgkill, getpid(), worker, SIGUSR2);
    });
    const auto slow = lua.run(callOf("slow", kNumberIn, kNumberOut, numbers({7})));
    staller.join();
    sigaction(SIGUSR2, &previous, nullptr);
    ASSERT_TRUE(slow) << slow.error().message;
    EXPECT_EQ(*slow, numbers({7}));
    EXPECT_EQ(lua.starts(), 1);
#endif
}

TEST_F(LuaNodeHelperTest, AHelperCrashStopsTheRunAndTheNextBatchStartsANewOne)
{
    LuaNodeHelper lua(fake());
    const auto crashed = lua.run(callOf("crash", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(crashed);
    EXPECT_EQ(crashed.error().message, "the Lua helper stopped");
    EXPECT_TRUE(crashed.error().stopsRun);

    const auto failed = lua.run(callOf("fail", kNumberIn, kNumberOut, numbers({1})));
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().message, "code:1: fake failure");
    EXPECT_FALSE(failed.error().stopsRun);
    EXPECT_EQ(lua.starts(), 2);
}

TEST_F(LuaNodeHelperTest, AGraphSendsOneBatchPerLineWithAllItsRows)
{
    const core::Document doc = documentWithGraph(luaGraph("rows"));
    LuaNodeHelper lua(fake());
    const auto run = runGraph(doc, lua, true);
    ASSERT_TRUE(run);
    EXPECT_TRUE(run->problems.empty());
    std::vector<int> expected(7, 7);
    expected.insert(expected.end(), 10, 5);
    EXPECT_EQ(layers(*run), expected);
}

TEST_F(LuaNodeHelperTest, AStuckLineStopsTheWholeGraphRun)
{
    const core::Document doc = documentWithGraph(luaGraph("hang"));
    LuaNodeHelper lua(fake(300));
    const auto run = runGraph(doc, lua, true);
    ASSERT_TRUE(run);
    EXPECT_FALSE(run->ran);
    EXPECT_TRUE(run->lines.empty());
    ASSERT_EQ(run->problems.size(), 1u);
    EXPECT_EQ(run->problems[0].message, "took longer than 0.3 seconds and was stopped");
    EXPECT_EQ(lua.starts(), 1);
}

TEST_F(LuaNodeHelperTest, AGraphWithALuaNodeRunsEndToEndInTheSandboxedHelper)
{
    const core::Document doc = documentWithGraph(luaGraph("out = i * 2"));
    LuaNodeHelper lua(real());
    const auto refused = runGraph(doc, lua, false);
    ASSERT_TRUE(refused);
    EXPECT_FALSE(refused->ran);
    ASSERT_EQ(refused->problems.size(), 1u);
    EXPECT_EQ(refused->problems[0].node, "double");
    EXPECT_EQ(refused->problems[0].message, "runs only once you trust this Document's Lua");
    EXPECT_EQ(lua.starts(), 0);

    const auto run = runGraph(doc, lua, true);
    ASSERT_TRUE(run);
    EXPECT_TRUE(run->ran);
    EXPECT_TRUE(run->problems.empty()) << run->problems.front().message;
    Doubling doubling;
    const auto inProcess = runGraph(doc, doubling, true);
    ASSERT_TRUE(inProcess);
    EXPECT_EQ(run->lines.size(), 17u);
    EXPECT_EQ(layers(*run), layers(*inProcess));
    EXPECT_EQ(lua.starts(), 1);
}
