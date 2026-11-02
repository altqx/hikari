#include "hikari/backends/ai_network.h"
#include "hikari/backends/ai_wire.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <functional>

namespace app = hikari::application::ai;
using hikari::backends::ai::JsonConfigStore;
using hikari::backends::ai::NetworkProviderPort;
using hikari::backends::ai::SseParser;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

// Answers each request with the next reply in its queue, one write per chunk.
class MockProvider : public QObject {
public:
    struct Reply {
        int status = 200;
        QByteArray contentType = "text/event-stream";
        QList<QByteArray> chunks;
        enum class End { Close, Reset, Hang } end = End::Close;
    };
    struct Received {
        QByteArray method;
        QByteArray path;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
        QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
    };

    MockProvider()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                ++connections;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this] { ++disconnects; });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    std::string base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()).toStdString(); }

    std::deque<Reply> replies;
    QList<Received> received;
    int connections = 0;
    int disconnects = 0;

private:
    void read(QTcpSocket *socket)
    {
        QByteArray data = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", data);
        const qsizetype headerEnd = data.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        Received r;
        const QList<QByteArray> lines = data.left(headerEnd).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        r.method = first.value(0);
        r.path = first.value(1);
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            if (colon > 0)
                r.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const qsizetype length = r.headers.value("content-length").toLongLong();
        if (data.size() < headerEnd + 4 + length)
            return;
        r.body = data.mid(headerEnd + 4, length);
        received.append(r);
        socket->setProperty("request", QByteArray());
        const Reply reply = replies.empty() ? Reply{.status = 500} : replies.front();
        if (!replies.empty())
            replies.pop_front();
        socket->write("HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\nContent-Type: " + reply.contentType +
                      "\r\nConnection: close\r\n\r\n");
        auto queue = std::make_shared<QList<QByteArray>>(reply.chunks);
        auto step = std::make_shared<std::function<void()>>();
        QPointer<QTcpSocket> guarded(socket);
        *step = [guarded, queue, step, end = reply.end] {
            if (!guarded)
                return;
            if (!queue->isEmpty()) {
                guarded->write(queue->takeFirst());
                guarded->flush();
                QTimer::singleShot(5, guarded, *step);
                return;
            }
            if (end == Reply::End::Close)
                guarded->disconnectFromHost();
            else if (end == Reply::End::Reset)
                guarded->abort();
        };
        (*step)();
    }

    QTcpServer m_server;
};

QByteArray sse(const QByteArray &event, const QJsonObject &data)
{
    QByteArray out;
    if (!event.isEmpty())
        out += "event: " + event + "\n";
    return out + "data: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n\n";
}

QByteArray data(const QJsonObject &o) { return sse({}, o); }

QJsonObject obj(const char *json) { return QJsonDocument::fromJson(json).object(); }

app::Provider provider(app::Wire wire, const MockProvider &mock)
{
    app::Provider p;
    p.id = "mock";
    p.name = "Mock";
    p.preset = wire == app::Wire::AnthropicMessages ? app::Preset::Anthropic
               : wire == app::Wire::Responses     ? app::Preset::OpenAI
                                                  : app::Preset::OpenRouter;
    p.wire = wire;
    p.baseUrl = mock.base();
    return p;
}

constexpr char kKey[] = "sk-test-0123456789abcdef";

struct Streamed {
    std::vector<app::Event> events;
    bool done() const
    {
        return !events.empty() &&
               (std::holds_alternative<app::Finished>(events.back()) || std::holds_alternative<app::Failed>(events.back()));
    }
    std::string text() const
    {
        std::string s;
        for (const app::Event &e : events)
            if (const auto *d = std::get_if<app::TextDelta>(&e))
                s += d->text;
        return s;
    }
    const app::Finished &finished() const { return std::get<app::Finished>(events.back()); }
    const app::Error &error() const { return std::get<app::Failed>(events.back()).error; }
    int terminals() const
    {
        int n = 0;
        for (const app::Event &e : events)
            n += std::holds_alternative<app::Finished>(e) || std::holds_alternative<app::Failed>(e);
        return n;
    }
};

// Splits each chunk in two at an awkward byte, so lines arrive in pieces.
QList<QByteArray> ragged(const QList<QByteArray> &chunks)
{
    QList<QByteArray> out;
    for (const QByteArray &c : chunks) {
        const qsizetype at = c.size() / 3;
        out << c.left(at) << c.mid(at);
    }
    return out;
}

class AiProviderTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "ai_provider_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    void SetUp() override
    {
        port.setResolver([this](const QString &host, std::function<void(QList<QHostAddress>)> done) {
            resolved.append(host);
            done(names.value(host));
        });
        port.setLocalAddresses([this] { return localAddresses; });
        s_messages.clear();
        m_previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) {
            s_messages.append(m);
        });
    }
    void TearDown() override
    {
        qInstallMessageHandler(m_previous);
        for (const QString &m : s_messages)
            EXPECT_FALSE(m.contains(QLatin1String(kKey))) << "a key reached the log";
    }

    Streamed stream(app::Wire wire, const app::Request &request = {.model = "m", .messages = {{.parts = {app::Text{"Hi"}}}}})
    {
        Streamed run;
        port.stream(provider(wire, mock), kKey, request, [&](const app::Event &e) { run.events.push_back(e); });
        EXPECT_TRUE(waitFor([&] { return run.done(); }));
        return run;
    }

    MockProvider mock;
    NetworkProviderPort port;
    QHash<QString, QList<QHostAddress>> names;
    QStringList resolved;
    QList<QHostAddress> localAddresses{QHostAddress(QStringLiteral("192.168.1.10"))};
    static inline QStringList s_messages;
    QtMessageHandler m_previous = nullptr;
};

app::Request fullRequest()
{
    app::Request r;
    r.model = "the-model";
    r.system = "Be brief.";
    r.maxOutputTokens = 100;
    r.tools = {{.name = "get_line", .description = "Reads a Line", .parametersJson = R"({"type":"object","properties":{"n":{"type":"integer"}}})"}};
    r.messages = {
        {.author = app::Author::User, .parts = {app::Text{"What is on screen?"}, app::Image{"image/png", "iVBORw0KGgo="}}},
        {.author = app::Author::Assistant,
         .parts = {app::Text{"Let me look."}, app::ToolCall{"call_1", "get_line", R"({"n":3})"}}},
        {.author = app::Author::User, .parts = {app::ToolResult{"call_1", "Dialogue: 0,...", false}}},
    };
    return r;
}

}

TEST(AiSse, ParsesFieldsAcrossChunksAndLineEndings)
{
    SseParser parser;
    QList<SseParser::Message> all;
    for (const QByteArray &piece : QList<QByteArray>{"\xEF\xBB", "\xBF: comment\r\nevent: a\r", "\ndata: one\rdata:two\n",
                                                     "\n", "data: [DONE]\n", "\n", "id: 7\nretry: 5\n\n"})
        all += parser.feed(piece);
    ASSERT_EQ(all.size(), 2);
    EXPECT_EQ(all[0].event, "a");
    EXPECT_EQ(all[0].data, "one\ntwo");
    EXPECT_EQ(all[1].event, "message");
    EXPECT_EQ(all[1].data, "[DONE]");
}

TEST(AiSse, AnEventWithoutDataIsDropped)
{
    SseParser parser;
    EXPECT_TRUE(parser.feed("event: ping\n\n").isEmpty());
    const QList<SseParser::Message> next = parser.feed("data:x\n\n");
    ASSERT_EQ(next.size(), 1);
    EXPECT_EQ(next[0].event, "message");
}

TEST_F(AiProviderTest, ChatCompletionsStreamsTextAndUsage)
{
    mock.replies.push_back({.chunks = ragged({
                                ": OPENROUTER PROCESSING\n\n",
                                data(obj(R"({"choices":[{"delta":{"role":"assistant","content":"Hel"}}]})")),
                                data(obj(R"({"choices":[{"delta":{"content":"lo"},"finish_reason":"stop"}]})")),
                                data(obj(R"({"choices":[],"usage":{"prompt_tokens":12,"completion_tokens":3,"prompt_tokens_details":{"cached_tokens":4},"cost":0.00042}})")),
                                "data: [DONE]\n\n",
                            })});
    const Streamed run = stream(app::Wire::ChatCompletions);
    EXPECT_EQ(run.text(), "Hello");
    EXPECT_EQ(run.terminals(), 1);
    EXPECT_EQ(run.finished().reason, app::StopReason::End);
    EXPECT_EQ(run.finished().text, "Hello");
    EXPECT_EQ(run.finished().usage, (app::Usage{.inputTokens = 12, .outputTokens = 3, .cachedInputTokens = 4, .reportedCost = 0.00042}));
    ASSERT_EQ(mock.received.size(), 1);
    EXPECT_EQ(mock.received[0].path, "/v1/chat/completions");
    EXPECT_EQ(mock.received[0].headers.value("authorization"), QByteArray("Bearer ") + kKey);
    EXPECT_EQ(mock.received[0].headers.value("x-title"), "HikariSub");
    EXPECT_TRUE(mock.received[0].json().value("stream").toBool());
}

TEST_F(AiProviderTest, ChatCompletionsStreamsToolCalls)
{
    mock.replies.push_back({.chunks = {
                                data(obj(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_a","type":"function","function":{"name":"get_line","arguments":""}}]}}]})")),
                                data(obj(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"n\":"}}]}}]})")),
                                data(obj(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"3}"}},{"index":1,"id":"call_b","function":{"name":"seek","arguments":"{}"}}]}}]})")),
                                data(obj(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})")),
                                "data: [DONE]\n\n",
                            }});
    const Streamed run = stream(app::Wire::ChatCompletions);
    EXPECT_EQ(run.finished().reason, app::StopReason::ToolUse);
    EXPECT_EQ(run.finished().toolCalls,
              (std::vector<app::ToolCall>{{"call_a", "get_line", R"({"n":3})"}, {"call_b", "seek", "{}"}}));
    const auto started = std::get<app::ToolCallStarted>(run.events.front());
    EXPECT_EQ(started.id, "call_a");
    EXPECT_EQ(started.name, "get_line");
}

TEST_F(AiProviderTest, ChatCompletionsEncodesVisionToolsAndHistory)
{
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})")), "data: [DONE]\n\n"}});
    stream(app::Wire::ChatCompletions, fullRequest());
    const QJsonObject body = mock.received.at(0).json();
    EXPECT_EQ(body.value("model").toString(), "the-model");
    EXPECT_EQ(body.value("max_tokens").toInt(), 100);
    const QJsonArray messages = body.value("messages").toArray();
    ASSERT_EQ(messages.size(), 4);
    EXPECT_EQ(messages[0].toObject().value("role").toString(), "system");
    const QJsonArray user = messages[1].toObject().value("content").toArray();
    EXPECT_EQ(user[1].toObject().value("image_url").toObject().value("url").toString(), "data:image/png;base64,iVBORw0KGgo=");
    const QJsonObject assistant = messages[2].toObject();
    EXPECT_EQ(assistant.value("content").toString(), "Let me look.");
    EXPECT_EQ(assistant.value("tool_calls").toArray()[0].toObject().value("function").toObject().value("arguments").toString(),
              R"({"n":3})");
    EXPECT_EQ(messages[3].toObject().value("role").toString(), "tool");
    EXPECT_EQ(messages[3].toObject().value("tool_call_id").toString(), "call_1");
    const QJsonObject tool = body.value("tools").toArray()[0].toObject().value("function").toObject();
    EXPECT_EQ(tool.value("name").toString(), "get_line");
    EXPECT_EQ(tool.value("parameters").toObject().value("type").toString(), "object");
}

TEST_F(AiProviderTest, ResponsesStreamsTextToolCallsAndUsage)
{
    mock.replies.push_back({.chunks = ragged({
                                sse("response.created", obj(R"({"type":"response.created","response":{"id":"r"}})")),
                                sse("response.output_text.delta", obj(R"({"type":"response.output_text.delta","output_index":0,"delta":"Look"})")),
                                sse("response.output_item.added", obj(R"({"type":"response.output_item.added","output_index":1,"item":{"type":"function_call","id":"fc_1","call_id":"call_x","name":"get_line","arguments":""}})")),
                                sse("response.function_call_arguments.delta", obj(R"({"type":"response.function_call_arguments.delta","output_index":1,"item_id":"fc_1","delta":"{\"n\":1}"})")),
                                sse("response.completed", obj(R"({"type":"response.completed","response":{"status":"completed","usage":{"input_tokens":50,"output_tokens":7,"input_tokens_details":{"cached_tokens":10}}}})")),
                            })});
    const Streamed run = stream(app::Wire::Responses);
    EXPECT_EQ(run.text(), "Look");
    EXPECT_EQ(run.finished().reason, app::StopReason::ToolUse);
    EXPECT_EQ(run.finished().toolCalls, (std::vector<app::ToolCall>{{"call_x", "get_line", R"({"n":1})"}}));
    EXPECT_EQ(run.finished().usage, (app::Usage{.inputTokens = 50, .outputTokens = 7, .cachedInputTokens = 10}));
    EXPECT_EQ(mock.received.at(0).path, "/v1/responses");
}

TEST_F(AiProviderTest, ResponsesEncodesVisionToolsAndHistory)
{
    mock.replies.push_back({.chunks = {sse("response.completed", obj(R"({"type":"response.completed","response":{"usage":{}}})"))}});
    stream(app::Wire::Responses, fullRequest());
    const QJsonObject body = mock.received.at(0).json();
    EXPECT_FALSE(body.value("store").toBool(true));
    EXPECT_EQ(body.value("instructions").toString(), "Be brief.");
    EXPECT_EQ(body.value("max_output_tokens").toInt(), 100);
    const QJsonArray input = body.value("input").toArray();
    ASSERT_EQ(input.size(), 4);
    EXPECT_EQ(input[0].toObject().value("content").toArray()[1].toObject().value("type").toString(), "input_image");
    EXPECT_EQ(input[1].toObject().value("content").toArray()[0].toObject().value("type").toString(), "output_text");
    EXPECT_EQ(input[2].toObject().value("type").toString(), "function_call");
    EXPECT_EQ(input[2].toObject().value("call_id").toString(), "call_1");
    EXPECT_EQ(input[3].toObject().value("type").toString(), "function_call_output");
    EXPECT_EQ(body.value("tools").toArray()[0].toObject().value("name").toString(), "get_line");
}

TEST_F(AiProviderTest, AnthropicStreamsTextToolCallsAndUsage)
{
    mock.replies.push_back({.chunks = ragged({
                                sse("message_start", obj(R"({"type":"message_start","message":{"usage":{"input_tokens":20,"cache_read_input_tokens":30,"cache_creation_input_tokens":0,"output_tokens":1}}})")),
                                sse("content_block_start", obj(R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})")),
                                sse("ping", obj(R"({"type":"ping"})")),
                                sse("content_block_delta", obj(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"On it"}})")),
                                sse("content_block_stop", obj(R"({"type":"content_block_stop","index":0})")),
                                sse("content_block_start", obj(R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_1","name":"get_line","input":{}}})")),
                                sse("content_block_delta", obj(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"n\":"}})")),
                                sse("content_block_delta", obj(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"2}"}})")),
                                sse("message_delta", obj(R"({"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":15}})")),
                                sse("message_stop", obj(R"({"type":"message_stop"})")),
                            })});
    const Streamed run = stream(app::Wire::AnthropicMessages);
    EXPECT_EQ(run.text(), "On it");
    EXPECT_EQ(run.finished().reason, app::StopReason::ToolUse);
    EXPECT_EQ(run.finished().toolCalls, (std::vector<app::ToolCall>{{"toolu_1", "get_line", R"({"n":2})"}}));
    EXPECT_EQ(run.finished().usage, (app::Usage{.inputTokens = 50, .outputTokens = 15, .cachedInputTokens = 30}));
    const MockProvider::Received &r = mock.received.at(0);
    EXPECT_EQ(r.path, "/v1/messages");
    EXPECT_EQ(r.headers.value("x-api-key"), kKey);
    EXPECT_EQ(r.headers.value("anthropic-version"), "2023-06-01");
    EXPECT_FALSE(r.headers.contains("authorization"));
}

TEST_F(AiProviderTest, AnthropicEncodesVisionToolsAndHistory)
{
    mock.replies.push_back({.chunks = {sse("message_stop", obj(R"({"type":"message_stop"})"))}});
    stream(app::Wire::AnthropicMessages, fullRequest());
    const QJsonObject body = mock.received.at(0).json();
    EXPECT_EQ(body.value("system").toArray()[0].toObject().value("text").toString(), "Be brief.");
    EXPECT_EQ(body.value("max_tokens").toInt(), 100);
    const QJsonArray messages = body.value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    const QJsonObject image = messages[0].toObject().value("content").toArray()[1].toObject();
    EXPECT_EQ(image.value("source").toObject().value("media_type").toString(), "image/png");
    const QJsonObject use = messages[1].toObject().value("content").toArray()[1].toObject();
    EXPECT_EQ(use.value("type").toString(), "tool_use");
    EXPECT_EQ(use.value("input").toObject().value("n").toInt(), 3);
    const QJsonObject result = messages[2].toObject().value("content").toArray()[0].toObject();
    EXPECT_EQ(result.value("type").toString(), "tool_result");
    EXPECT_EQ(result.value("tool_use_id").toString(), "call_1");
    EXPECT_EQ(body.value("tools").toArray()[0].toObject().value("input_schema").toObject().value("type").toString(), "object");
}

namespace {

app::Request cachedTurn(int turn)
{
    app::Request r = fullRequest();
    r.context = "Styles: Default, Sign. 1200 Lines.";
    r.cacheKey = "thread-7";
    r.tools.push_back({.name = "add_line", .description = "Adds a Line", .parametersJson = R"({"type":"object"})"});
    if (turn == 2) {
        std::ranges::reverse(r.tools);
        r.messages.push_back({.author = app::Author::Assistant, .parts = {app::Text{"Line 3 is a sign."}}});
        r.messages.push_back({.author = app::Author::User, .parts = {app::Text{"Time it."}}});
    }
    return r;
}

QByteArray compactValue(const QJsonValue &v)
{
    return QJsonDocument(v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject())).toJson(QJsonDocument::Compact);
}

QJsonArray withoutCacheMarks(QJsonArray messages)
{
    for (qsizetype i = 0; i < messages.size(); ++i) {
        QJsonObject m = messages[i].toObject();
        if (m.value("content").isArray()) {
            QJsonArray content = m.value("content").toArray();
            for (qsizetype j = 0; j < content.size(); ++j) {
                QJsonObject block = content[j].toObject();
                block.remove("cache_control");
                content.replace(j, block);
            }
            m.insert("content", content);
        }
        messages.replace(i, m);
    }
    return messages;
}

int cacheMarks(const QByteArray &body) { return static_cast<int>(body.count("\"cache_control\"")); }

QJsonArray firstOf(const QJsonArray &array, qsizetype n)
{
    QJsonArray out;
    for (qsizetype i = 0; i < n && i < array.size(); ++i)
        out.append(array[i]);
    return out;
}

}

TEST_F(AiProviderTest, AnthropicMarksTheStablePrefixAndTheLastTurn)
{
    app::Request request = cachedTurn(1);
    request.cacheLifetime = app::CacheLifetime::Hour;
    mock.replies.push_back({.chunks = {sse("message_stop", obj(R"({"type":"message_stop"})"))}});
    stream(app::Wire::AnthropicMessages, request);
    const QJsonObject body = mock.received.at(0).json();
    const QJsonObject hour = obj(R"({"type":"ephemeral","ttl":"1h"})");
    const QJsonArray system = body.value("system").toArray();
    ASSERT_EQ(system.size(), 2);
    EXPECT_EQ(system[0].toObject(), obj(R"({"type":"text","text":"Be brief."})"));
    EXPECT_EQ(system[1].toObject().value("text").toString(), "Styles: Default, Sign. 1200 Lines.");
    EXPECT_EQ(system[1].toObject().value("cache_control").toObject(), hour);
    const QJsonArray messages = body.value("messages").toArray();
    EXPECT_EQ(messages.last().toObject().value("content").toArray().last().toObject().value("cache_control").toObject(), hour);
    EXPECT_EQ(cacheMarks(mock.received.at(0).body), 2) << "Anthropic allows four";

    app::Request bare = cachedTurn(1);
    bare.system.clear();
    bare.context.clear();
    mock.replies.push_back({.chunks = {sse("message_stop", obj(R"({"type":"message_stop"})"))}});
    stream(app::Wire::AnthropicMessages, bare);
    const QJsonObject second = mock.received.at(1).json();
    EXPECT_FALSE(second.contains("system"));
    EXPECT_EQ(second.value("tools").toArray().last().toObject().value("cache_control").toObject(),
              obj(R"({"type":"ephemeral"})"));
    EXPECT_EQ(cacheMarks(mock.received.at(1).body), 2);
}

TEST_F(AiProviderTest, TheStablePrefixIsByteIdenticalAcrossTurns)
{
    struct Case {
        app::Wire wire;
        app::Preset preset;
        std::string model;
        QByteArray end;
    };
    const Case cases[] = {
        {app::Wire::AnthropicMessages, app::Preset::Anthropic, "claude", sse("message_stop", obj(R"({"type":"message_stop"})"))},
        {app::Wire::Responses, app::Preset::OpenAI, "gpt",
         sse("response.completed", obj(R"({"type":"response.completed","response":{"usage":{}}})"))},
        {app::Wire::ChatCompletions, app::Preset::OpenRouter, "anthropic/claude",
         data(obj(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})")) + "data: [DONE]\n\n"},
    };
    for (const Case &c : cases) {
        mock.received.clear();
        for (const int turn : {1, 2}) {
            mock.replies.push_back({.chunks = {c.end}});
            app::Provider p = provider(c.wire, mock);
            p.preset = c.preset;
            app::Request r = cachedTurn(turn);
            r.model = c.model;
            Streamed run;
            port.stream(p, kKey, r, [&](const app::Event &e) { run.events.push_back(e); });
            ASSERT_TRUE(waitFor([&] { return run.done(); }));
        }
        ASSERT_EQ(mock.received.size(), 2);
        const QJsonObject one = mock.received[0].json();
        const QJsonObject two = mock.received[1].json();
        SCOPED_TRACE(c.model);
        EXPECT_EQ(compactValue(one.value("tools")), compactValue(two.value("tools")));
        EXPECT_EQ(one.value("tools").toArray().size(), 2);
        if (c.wire == app::Wire::AnthropicMessages) {
            EXPECT_EQ(compactValue(one.value("system")), compactValue(two.value("system")));
            const QJsonArray a = withoutCacheMarks(one.value("messages").toArray());
            const QJsonArray b = withoutCacheMarks(two.value("messages").toArray());
            EXPECT_EQ(compactValue(a), compactValue(firstOf(b, a.size())));
        } else if (c.wire == app::Wire::Responses) {
            EXPECT_EQ(one.value("instructions"), two.value("instructions"));
            EXPECT_EQ(one.value("prompt_cache_key").toString(), "thread-7");
            EXPECT_EQ(one.value("prompt_cache_key"), two.value("prompt_cache_key"));
            const QJsonArray a = one.value("input").toArray();
            const QJsonArray b = two.value("input").toArray();
            EXPECT_EQ(a[0].toObject().value("role").toString(), "developer");
            EXPECT_EQ(compactValue(a), compactValue(firstOf(b, a.size())));
        } else {
            EXPECT_EQ(one.value("prompt_cache_key").toString(), "thread-7");
            const QJsonArray a = one.value("messages").toArray();
            const QJsonArray b = two.value("messages").toArray();
            EXPECT_EQ(compactValue(firstOf(a, 2)), compactValue(firstOf(b, 2)));
            EXPECT_EQ(a[1].toObject().value("content").toArray()[0].toObject().value("cache_control").toObject(),
                      obj(R"({"type":"ephemeral"})"));
            EXPECT_EQ(compactValue(withoutCacheMarks(a)), compactValue(firstOf(withoutCacheMarks(b), a.size())));
        }
    }
}

TEST_F(AiProviderTest, CacheMarksGoOnlyWhereTheProviderTakesThem)
{
    const auto bodyFor = [&](app::Preset preset, app::Wire wire, const std::string &model) {
        mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})")), "data: [DONE]\n\n"}});
        app::Provider p = provider(wire, mock);
        p.preset = preset;
        app::Request r = cachedTurn(1);
        r.model = model;
        Streamed run;
        port.stream(p, kKey, r, [&](const app::Event &e) { run.events.push_back(e); });
        EXPECT_TRUE(waitFor([&] { return run.done(); }));
        return mock.received.last().body;
    };
    const QByteArray gemini = bodyFor(app::Preset::OpenRouter, app::Wire::ChatCompletions, "google/gemini-x");
    EXPECT_EQ(cacheMarks(gemini), 2);
    const QByteArray gpt = bodyFor(app::Preset::OpenRouter, app::Wire::ChatCompletions, "openai/gpt-x");
    EXPECT_EQ(cacheMarks(gpt), 0);
    EXPECT_TRUE(gpt.contains("\"prompt_cache_key\":\"thread-7\""));
    const QByteArray custom = bodyFor(app::Preset::Custom, app::Wire::ChatCompletions, "anthropic/claude");
    EXPECT_EQ(cacheMarks(custom), 0);
    EXPECT_FALSE(custom.contains("prompt_cache_key"));
    const QByteArray customResponses = bodyFor(app::Preset::Custom, app::Wire::Responses, "gpt");
    EXPECT_FALSE(customResponses.contains("prompt_cache_key"));
    EXPECT_EQ(cacheMarks(customResponses), 0);
}

TEST_F(AiProviderTest, CacheReadsAndWritesComeBackFromEveryWire)
{
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"a"},"finish_reason":"stop"}]})")),
                                       data(obj(R"({"choices":[],"usage":{"prompt_tokens":3000,"completion_tokens":5,"prompt_tokens_details":{"cached_tokens":2000,"cache_write_tokens":900}}})")),
                                       "data: [DONE]\n\n"}});
    EXPECT_EQ(stream(app::Wire::ChatCompletions).finished().usage,
              (app::Usage{.inputTokens = 3000, .outputTokens = 5, .cachedInputTokens = 2000, .cacheWriteTokens = 900}));

    mock.replies.push_back({.chunks = {sse("response.completed", obj(R"({"type":"response.completed","response":{"usage":{"input_tokens":4000,"output_tokens":9,"input_tokens_details":{"cached_tokens":3072,"cache_write_tokens":512}}}})"))}});
    EXPECT_EQ(stream(app::Wire::Responses).finished().usage,
              (app::Usage{.inputTokens = 4000, .outputTokens = 9, .cachedInputTokens = 3072, .cacheWriteTokens = 512}));

    mock.replies.push_back({.chunks = {sse("message_start", obj(R"({"type":"message_start","message":{"usage":{"input_tokens":50,"cache_read_input_tokens":0,"cache_creation_input_tokens":1800,"cache_creation":{"ephemeral_5m_input_tokens":1500,"ephemeral_1h_input_tokens":300},"output_tokens":1}}})")),
                                       sse("message_delta", obj(R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":4}})")),
                                       sse("message_stop", obj(R"({"type":"message_stop"})"))}});
    EXPECT_EQ(stream(app::Wire::AnthropicMessages).finished().usage,
              (app::Usage{.inputTokens = 1850, .outputTokens = 4, .cachedInputTokens = 0, .cacheWriteTokens = 1800, .cacheWriteHourTokens = 300}));
}

TEST_F(AiProviderTest, HttpErrorsCarryTheProvidersMessage)
{
    const std::pair<int, app::ErrorKind> cases[] = {{401, app::ErrorKind::Unauthorized},
                                                    {403, app::ErrorKind::Unauthorized},
                                                    {429, app::ErrorKind::RateLimited},
                                                    {400, app::ErrorKind::Rejected},
                                                    {503, app::ErrorKind::Server}};
    for (const app::Wire wire : {app::Wire::ChatCompletions, app::Wire::Responses, app::Wire::AnthropicMessages})
        for (const auto &[status, kind] : cases) {
            mock.replies.push_back({.status = status,
                                    .contentType = "application/json",
                                    .chunks = {R"({"error":{"type":"x","message":"nope )" + QByteArray::number(status) + "\"}}"}});
            const Streamed run = stream(wire);
            EXPECT_EQ(run.error().kind, kind) << status;
            EXPECT_EQ(run.error().httpStatus, status);
            EXPECT_EQ(run.error().detail, "nope " + std::to_string(status));
        }
}

TEST_F(AiProviderTest, ErrorsInsideTheStreamFailTheRequest)
{
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"a"}}]})")),
                                       data(obj(R"({"error":{"code":429,"message":"slow down"}})"))}});
    Streamed run = stream(app::Wire::ChatCompletions);
    EXPECT_EQ(run.error().kind, app::ErrorKind::RateLimited);
    EXPECT_EQ(run.error().detail, "slow down");

    mock.replies.push_back({.chunks = {sse("error", obj(R"({"type":"error","code":"subscription_sharing_usage_limit_exceeded","message":"limit"})"))}});
    run = stream(app::Wire::Responses);
    EXPECT_EQ(run.error().kind, app::ErrorKind::RateLimited);

    mock.replies.push_back({.chunks = {sse("error", obj(R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})"))}});
    run = stream(app::Wire::AnthropicMessages);
    EXPECT_EQ(run.error().kind, app::ErrorKind::Server);
    EXPECT_EQ(run.error().detail, "Overloaded");
    EXPECT_EQ(run.terminals(), 1);
}

TEST_F(AiProviderTest, AStreamThatStopsEarlyFails)
{
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"half"}}]})"))}});
    Streamed run = stream(app::Wire::ChatCompletions);
    EXPECT_EQ(run.error().kind, app::ErrorKind::Malformed);

    mock.replies.push_back({.chunks = {sse("content_block_delta", obj(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"x"}})"))},
                            .end = MockProvider::Reply::End::Reset});
    run = stream(app::Wire::AnthropicMessages);
    EXPECT_TRUE(run.error().kind == app::ErrorKind::Network || run.error().kind == app::ErrorKind::Malformed);

    mock.replies.push_back({.chunks = {"data: {not json\n\n"}});
    run = stream(app::Wire::Responses);
    EXPECT_EQ(run.error().kind, app::ErrorKind::Malformed);
}

TEST_F(AiProviderTest, ASilentStreamTimesOut)
{
    port.setIdleTimeout(std::chrono::milliseconds(300));
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"a"}}]})"))},
                            .end = MockProvider::Reply::End::Hang});
    const Streamed run = stream(app::Wire::ChatCompletions);
    EXPECT_EQ(run.text(), "a");
    EXPECT_EQ(run.error().kind, app::ErrorKind::Timeout);
    EXPECT_TRUE(waitFor([&] { return mock.disconnects == 1; }));
}

TEST_F(AiProviderTest, CancelClosesTheConnection)
{
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"a"}}]})"))},
                            .end = MockProvider::Reply::End::Hang});
    Streamed run;
    const app::StreamId id = port.stream(provider(app::Wire::ChatCompletions, mock), kKey, {.model = "m"},
                                         [&](const app::Event &e) { run.events.push_back(e); });
    ASSERT_TRUE(waitFor([&] { return !run.events.empty(); }));
    port.cancel(id);
    ASSERT_TRUE(run.done());
    EXPECT_EQ(run.error().kind, app::ErrorKind::Cancelled);
    EXPECT_TRUE(waitFor([&] { return mock.disconnects == 1; }));
    QCoreApplication::processEvents();
    EXPECT_EQ(run.terminals(), 1);
}

TEST(AiKeyRoutes, TailscaleRangesAndNames)
{
    using hikari::backends::ai::keySafeOverPlainHttp;
    using hikari::backends::ai::tailscaleName;
    const QList<QHostAddress> lanOnly{QHostAddress(QStringLiteral("192.168.1.10"))};
    const QList<QHostAddress> onTailnet{QHostAddress(QStringLiteral("192.168.1.10")),
                                        QHostAddress(QStringLiteral("100.101.102.103"))};
    const auto safe = [](const char *address, const QList<QHostAddress> &local) {
        return keySafeOverPlainHttp(QHostAddress(QString::fromLatin1(address)), local);
    };

    EXPECT_TRUE(safe("127.0.0.1", lanOnly));
    EXPECT_TRUE(safe("::1", lanOnly));
    EXPECT_TRUE(safe("100.64.0.1", onTailnet));
    EXPECT_TRUE(safe("100.127.255.254", onTailnet));
    EXPECT_TRUE(safe("::ffff:100.100.1.2", onTailnet));
    EXPECT_FALSE(safe("100.100.1.2", lanOnly)) << "shared CGNAT space without a tailnet address here";
    EXPECT_FALSE(safe("100.128.0.1", onTailnet));
    EXPECT_FALSE(safe("100.63.255.255", onTailnet));
    EXPECT_TRUE(safe("fd7a:115c:a1e0::1", lanOnly));
    EXPECT_TRUE(safe("fd7a:115c:a1e0:ab12:4843:cd96:6258:b240", lanOnly));
    EXPECT_FALSE(safe("fd7a:115c:a1e1::1", lanOnly));
    EXPECT_FALSE(safe("192.168.1.20", onTailnet));
    EXPECT_FALSE(safe("10.0.0.5", onTailnet));
    EXPECT_FALSE(safe("203.0.113.9", onTailnet));

    EXPECT_TRUE(tailscaleName(QStringLiteral("gpu-box.tail1234.ts.net")));
    EXPECT_TRUE(tailscaleName(QStringLiteral("GPU-Box.Tail1234.TS.NET.")));
    EXPECT_FALSE(tailscaleName(QStringLiteral("ts.net")));
    EXPECT_FALSE(tailscaleName(QStringLiteral(".ts.net")));
    EXPECT_FALSE(tailscaleName(QStringLiteral("evil-ts.net")));
    EXPECT_FALSE(tailscaleName(QStringLiteral("box.ts.net.example.com")));
}

TEST_F(AiProviderTest, AKeyTravelsOnlyOverHttpsOrToThisMachineOrATailnet)
{
    const auto attempt = [&](const std::string &base) {
        app::Provider p = provider(app::Wire::ChatCompletions, mock);
        p.baseUrl = base;
        Streamed run;
        port.stream(p, kKey, {.model = "m"}, [&](const app::Event &e) { run.events.push_back(e); });
        EXPECT_TRUE(waitFor([&] { return run.done(); }));
        return run.done() ? run.error().kind : app::ErrorKind::Cancelled;
    };
    EXPECT_EQ(attempt("http://192.168.1.20:8000/v1"), app::ErrorKind::InsecureKey);
    EXPECT_EQ(attempt("http://100.100.1.2:8000/v1"), app::ErrorKind::InsecureKey);
    names.insert(QStringLiteral("box.lan"), {QHostAddress(QStringLiteral("192.168.1.20"))});
    EXPECT_EQ(attempt("http://box.lan:8000/v1"), app::ErrorKind::InsecureKey);
    EXPECT_EQ(attempt("http://nowhere.example/v1"), app::ErrorKind::Network);

    std::optional<app::ProviderPort::ModelsResult> models;
    app::Provider lan = provider(app::Wire::ChatCompletions, mock);
    lan.baseUrl = "http://192.168.1.20/v1";
    port.listModels(lan, kKey, [&](app::ProviderPort::ModelsResult r) { models = r; });
    ASSERT_TRUE(models.has_value());
    EXPECT_EQ(std::get<app::Error>(*models).kind, app::ErrorKind::InsecureKey);
    EXPECT_EQ(mock.connections, 0);
}

TEST_F(AiProviderTest, ANameIsResolvedOnceAndTheCheckedAddressIsUsed)
{
    const QString port_ = QString::fromStdString(mock.base()).section(u':', 2).section(u'/', 0, 0);
    names.insert(QStringLiteral("peer.example"), {QHostAddress(QStringLiteral("192.168.1.20")), QHostAddress::LocalHost});
    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"ok"},"finish_reason":"stop"}]})")), "data: [DONE]\n\n"}});
    app::Provider p = provider(app::Wire::ChatCompletions, mock);
    p.baseUrl = "http://peer.example:" + port_.toStdString() + "/v1";
    Streamed run;
    port.stream(p, kKey, {.model = "m"}, [&](const app::Event &e) { run.events.push_back(e); });
    ASSERT_TRUE(waitFor([&] { return run.done(); }));
    EXPECT_EQ(run.text(), "ok");
    EXPECT_EQ(resolved, QStringList{QStringLiteral("peer.example")});
    ASSERT_EQ(mock.received.size(), 1);
    EXPECT_EQ(mock.received[0].headers.value("host"), "peer.example:" + port_.toLatin1());
}

TEST_F(AiProviderTest, AMagicDnsNameIsAllowedByName)
{
    Streamed run;
    app::Provider p = provider(app::Wire::ChatCompletions, mock);
    p.baseUrl = "http://gpu-box.tail1234.ts.net:1/v1";
    const app::StreamId id = port.stream(p, kKey, {.model = "m"}, [&](const app::Event &e) { run.events.push_back(e); });
    EXPECT_FALSE(run.done());
    EXPECT_TRUE(resolved.isEmpty());
    port.cancel(id);
    ASSERT_TRUE(run.done());
    EXPECT_EQ(run.error().kind, app::ErrorKind::Cancelled);
}

TEST_F(AiProviderTest, TheCatalogueReadsCapabilitiesAndPrices)
{
    mock.replies.push_back({.contentType = "application/json",
                            .chunks = {R"({"data":[
        {"id":"vendor/vision","name":"Vision","context_length":200000,
         "architecture":{"input_modalities":["text","image"]},"supported_parameters":["tools","temperature"],
         "pricing":{"prompt":"0.000003","completion":"0.000015","input_cache_read":"0.0000003","input_cache_write":"0.00000375","input_cache_write_1h":"0.000006"}},
        {"id":"vendor/plain","architecture":{"input_modalities":["text"]},"supported_parameters":[]},
        {"id":"claude-x","display_name":"Claude X","type":"model"},
        {"name":"no id"}]})"}});
    std::optional<app::ProviderPort::ModelsResult> result;
    port.listModels(provider(app::Wire::ChatCompletions, mock), kKey, [&](app::ProviderPort::ModelsResult r) { result = r; });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    const auto &models = std::get<std::vector<app::Model>>(*result);
    ASSERT_EQ(models.size(), 3u);
    EXPECT_EQ(models[0].capabilities, (app::Capabilities{.tools = true, .vision = true}));
    EXPECT_EQ(models[0].contextTokens, 200000);
    ASSERT_TRUE(models[0].price);
    EXPECT_NEAR(models[0].price->input, 3.0, 1e-9);
    EXPECT_NEAR(models[0].price->output, 15.0, 1e-9);
    EXPECT_NEAR(*models[0].price->cachedInput, 0.3, 1e-9);
    EXPECT_NEAR(*models[0].price->cacheWrite, 3.75, 1e-9);
    EXPECT_NEAR(*models[0].price->cacheWriteHour, 6.0, 1e-9);
    EXPECT_EQ(models[1].capabilities, (app::Capabilities{.tools = false, .vision = false}));
    EXPECT_EQ(models[1].name, "vendor/plain");
    EXPECT_EQ(models[2].name, "Claude X");
    EXPECT_EQ(models[2].capabilities, std::nullopt);
    EXPECT_EQ(mock.received.at(0).method, "GET");
    EXPECT_EQ(mock.received.at(0).path, "/v1/models");

    mock.replies.push_back({.contentType = "application/json", .chunks = {R"({"data":[]})"}});
    result.reset();
    port.listModels(provider(app::Wire::AnthropicMessages, mock), kKey, [&](app::ProviderPort::ModelsResult r) { result = r; });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_EQ(mock.received.at(1).path, "/v1/models?limit=1000");
    EXPECT_EQ(mock.received.at(1).headers.value("x-api-key"), kKey);

    mock.replies.push_back({.status = 401, .contentType = "application/json", .chunks = {R"({"error":{"message":"bad key"}})"}});
    result.reset();
    port.listModels(provider(app::Wire::Responses, mock), kKey, [&](app::ProviderPort::ModelsResult r) { result = r; });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_EQ(std::get<app::Error>(*result).kind, app::ErrorKind::Unauthorized);
    EXPECT_EQ(std::get<app::Error>(*result).detail, "bad key");
}

TEST_F(AiProviderTest, TheConfigFileKeepsEverythingButTheKey)
{
    struct Keys final : app::KeyStore {
        std::map<std::string, std::string> keys;
        void read(const std::string &id, std::function<void(app::KeyResult)> done) override { done(keys.at(id)); }
        void write(const std::string &id, const std::string &key, std::function<void(std::optional<app::KeyError>)> done) override
        {
            keys[id] = key;
            done(std::nullopt);
        }
        void remove(const std::string &id, std::function<void(std::optional<app::KeyError>)> done) override
        {
            keys.erase(id);
            done(std::nullopt);
        }
    } keys;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ai/providers.json"));
    JsonConfigStore store(path);
    app::Providers providers(store, keys, port);

    app::Provider p = provider(app::Wire::ChatCompletions, mock);
    p.models = {{.id = "m1", .name = "M1", .capabilities = app::Capabilities{.tools = true}, .price = app::Price{.input = 1, .output = 2, .cachedInput = 0.5, .cacheWrite = 1.25, .cacheWriteHour = 2}, .contextTokens = 8000}};
    p.capabilityOverrides["m2"] = {.tools = false, .vision = true};
    std::optional<std::optional<app::SaveError>> saved;
    providers.add(p, kKey, [&](std::optional<app::SaveError> e) { saved = e; });
    ASSERT_TRUE(saved.has_value());
    ASSERT_EQ(*saved, std::nullopt);
    ASSERT_TRUE(providers.assign(app::Role::Judge, {"mock", "m1"}));

    mock.replies.push_back({.chunks = {data(obj(R"({"choices":[{"delta":{"content":"ok"},"finish_reason":"stop"}]})")), "data: [DONE]\n\n"}});
    std::vector<app::Event> events;
    providers.stream(app::Role::Judge, {.messages = {{.parts = {app::Text{"x"}}}}}, [&](const app::Event &e) { events.push_back(e); });
    ASSERT_TRUE(waitFor([&] { return events.size() == 2; }));
    EXPECT_EQ(mock.received.at(0).json().value("model").toString(), "m1");

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    EXPECT_FALSE(text.contains(kKey));
    EXPECT_FALSE(text.contains("sk-"));
    EXPECT_EQ(JsonConfigStore(path).load(), providers.config());
}

TEST_F(AiProviderTest, WithoutAProviderNoConnectionIsMade)
{
    struct NoKeys final : app::KeyStore {
        void read(const std::string &, std::function<void(app::KeyResult)> done) override { done(app::KeyError::NotFound); }
        void write(const std::string &, const std::string &, std::function<void(std::optional<app::KeyError>)> done) override
        {
            done(app::KeyError::Unavailable);
        }
        void remove(const std::string &, std::function<void(std::optional<app::KeyError>)> done) override { done(std::nullopt); }
    } keys;
    QTemporaryDir dir;
    JsonConfigStore store(dir.filePath(QStringLiteral("providers.json")));
    app::Providers providers(store, keys, port);
    std::vector<app::Event> events;
    providers.stream(app::Role::Chat, {.messages = {{.parts = {app::Text{"x"}}}}}, [&](const app::Event &e) { events.push_back(e); });
    std::optional<std::optional<app::SaveError>> saved;
    providers.add(provider(app::Wire::ChatCompletions, mock), kKey, [&](std::optional<app::SaveError> e) { saved = e; });
    EXPECT_EQ(saved, std::optional<std::optional<app::SaveError>>(app::SaveError::KeychainUnavailable));
    providers.stream(app::Role::Chat, {.messages = {{.parts = {app::Text{"x"}}}}}, [&](const app::Event &e) { events.push_back(e); });
    QCoreApplication::processEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(std::get<app::Failed>(events[0]).error.kind, app::ErrorKind::NoProvider);
    EXPECT_EQ(std::get<app::Failed>(events[1]).error.kind, app::ErrorKind::NoProvider);
    EXPECT_EQ(mock.connections, 0);
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("providers.json"))));
}
