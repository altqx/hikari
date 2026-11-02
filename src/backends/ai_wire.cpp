#include "hikari/backends/ai_wire.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace hikari::backends::ai {

namespace {

QString q(const std::string &s) { return QString::fromStdString(s); }

QJsonObject schemaObject(const std::string &json)
{
    const QJsonObject parsed = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
    return parsed.isEmpty() ? QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}} : parsed;
}

QJsonObject argumentsObject(const std::string &json)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

QString dataUrl(const app::Image &image)
{
    return QStringLiteral("data:%1;base64,%2").arg(q(image.mediaType), q(image.base64));
}

template <typename T, typename F>
void forEach(const app::Message &message, F &&f)
{
    for (const app::Part &part : message.parts)
        if (const auto *p = std::get_if<T>(&part))
            f(*p);
}

QByteArray compact(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

std::vector<std::pair<QByteArray, QByteArray>> authHeaders(const app::Provider &provider, const QByteArray &key)
{
    std::vector<std::pair<QByteArray, QByteArray>> headers;
    if (provider.wire == app::Wire::AnthropicMessages) {
        headers.emplace_back("anthropic-version", "2023-06-01");
        if (!key.isEmpty())
            headers.emplace_back("x-api-key", key);
    } else if (provider.wire == app::Wire::Gemini) {
        if (!key.isEmpty())
            headers.emplace_back("x-goog-api-key", key);
    } else if (!key.isEmpty()) {
        headers.emplace_back("Authorization", "Bearer " + key);
    }
    if (provider.preset == app::Preset::OpenRouter) {
        headers.emplace_back("HTTP-Referer", "https://hikarisub.altqx.com");
        headers.emplace_back("X-Title", "HikariSub");
    }
    return headers;
}

QJsonArray chatMessages(const app::Request &request)
{
    QJsonArray out;
    if (!request.system.empty())
        out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                               {QStringLiteral("content"), q(request.system)}});
    for (const app::Message &message : request.messages) {
        if (message.author == app::Author::Assistant) {
            QString text;
            QJsonArray calls;
            forEach<app::Text>(message, [&](const app::Text &t) { text += q(t.text); });
            forEach<app::ToolCall>(message, [&](const app::ToolCall &c) {
                calls.append(QJsonObject{{QStringLiteral("id"), q(c.id)},
                                         {QStringLiteral("type"), QStringLiteral("function")},
                                         {QStringLiteral("function"),
                                          QJsonObject{{QStringLiteral("name"), q(c.name)},
                                                      {QStringLiteral("arguments"), q(c.argumentsJson)}}}});
            });
            QJsonObject m{{QStringLiteral("role"), QStringLiteral("assistant")},
                          {QStringLiteral("content"), text.isEmpty() ? QJsonValue() : QJsonValue(text)}};
            if (!calls.isEmpty())
                m.insert(QStringLiteral("tool_calls"), calls);
            out.append(m);
            continue;
        }
        forEach<app::ToolResult>(message, [&](const app::ToolResult &r) {
            out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                   {QStringLiteral("tool_call_id"), q(r.callId)},
                                   {QStringLiteral("content"), q(r.content)}});
        });
        QJsonArray content;
        for (const app::Part &part : message.parts) {
            if (const auto *t = std::get_if<app::Text>(&part))
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                           {QStringLiteral("text"), q(t->text)}});
            else if (const auto *i = std::get_if<app::Image>(&part))
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                                           {QStringLiteral("image_url"),
                                            QJsonObject{{QStringLiteral("url"), dataUrl(*i)}}}});
        }
        if (!content.isEmpty())
            out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}});
    }
    return out;
}

QByteArray chatBody(const app::Provider &provider, const app::Request &request)
{
    QJsonObject body{{QStringLiteral("model"), q(request.model)},
                     {QStringLiteral("messages"), chatMessages(request)},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}}};
    if (provider.preset == app::Preset::OpenRouter)
        body.insert(QStringLiteral("usage"), QJsonObject{{QStringLiteral("include"), true}});
    if (request.maxOutputTokens)
        body.insert(QStringLiteral("max_tokens"), *request.maxOutputTokens);
    QJsonArray tools;
    for (const app::ToolDefinition &tool : request.tools)
        tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                 {QStringLiteral("function"),
                                  QJsonObject{{QStringLiteral("name"), q(tool.name)},
                                              {QStringLiteral("description"), q(tool.description)},
                                              {QStringLiteral("parameters"), schemaObject(tool.parametersJson)}}}});
    if (!tools.isEmpty())
        body.insert(QStringLiteral("tools"), tools);
    return compact(body);
}

QByteArray responsesBody(const app::Request &request)
{
    QJsonArray input;
    for (const app::Message &message : request.messages) {
        if (message.author == app::Author::Assistant) {
            for (const app::Part &part : message.parts) {
                if (const auto *t = std::get_if<app::Text>(&part))
                    input.append(QJsonObject{
                        {QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"),
                         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("output_text")},
                                                {QStringLiteral("text"), q(t->text)}}}}});
                else if (const auto *c = std::get_if<app::ToolCall>(&part))
                    input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                                             {QStringLiteral("call_id"), q(c->id)},
                                             {QStringLiteral("name"), q(c->name)},
                                             {QStringLiteral("arguments"), q(c->argumentsJson)}});
            }
            continue;
        }
        forEach<app::ToolResult>(message, [&](const app::ToolResult &r) {
            input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call_output")},
                                     {QStringLiteral("call_id"), q(r.callId)},
                                     {QStringLiteral("output"), q(r.content)}});
        });
        QJsonArray content;
        for (const app::Part &part : message.parts) {
            if (const auto *t = std::get_if<app::Text>(&part))
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                           {QStringLiteral("text"), q(t->text)}});
            else if (const auto *i = std::get_if<app::Image>(&part))
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                                           {QStringLiteral("image_url"), dataUrl(*i)}});
        }
        if (!content.isEmpty())
            input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}});
    }
    QJsonObject body{{QStringLiteral("model"), q(request.model)},
                     {QStringLiteral("input"), input},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("store"), false}};
    if (!request.system.empty())
        body.insert(QStringLiteral("instructions"), q(request.system));
    if (request.maxOutputTokens)
        body.insert(QStringLiteral("max_output_tokens"), *request.maxOutputTokens);
    QJsonArray tools;
    for (const app::ToolDefinition &tool : request.tools)
        tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                 {QStringLiteral("name"), q(tool.name)},
                                 {QStringLiteral("description"), q(tool.description)},
                                 {QStringLiteral("parameters"), schemaObject(tool.parametersJson)},
                                 {QStringLiteral("strict"), false}});
    if (!tools.isEmpty())
        body.insert(QStringLiteral("tools"), tools);
    return compact(body);
}

QByteArray anthropicBody(const app::Request &request)
{
    constexpr int kDefaultMaxTokens = 8192;
    QJsonArray messages;
    for (const app::Message &message : request.messages) {
        QJsonArray content;
        if (message.author == app::Author::User)
            forEach<app::ToolResult>(message, [&](const app::ToolResult &r) {
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                           {QStringLiteral("tool_use_id"), q(r.callId)},
                                           {QStringLiteral("content"), q(r.content)},
                                           {QStringLiteral("is_error"), r.isError}});
            });
        for (const app::Part &part : message.parts) {
            if (const auto *t = std::get_if<app::Text>(&part)) {
                if (!t->text.empty())
                    content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                               {QStringLiteral("text"), q(t->text)}});
            } else if (const auto *i = std::get_if<app::Image>(&part)) {
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                           {QStringLiteral("source"),
                                            QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                                        {QStringLiteral("media_type"), q(i->mediaType)},
                                                        {QStringLiteral("data"), q(i->base64)}}}});
            } else if (const auto *c = std::get_if<app::ToolCall>(&part)) {
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                           {QStringLiteral("id"), q(c->id)},
                                           {QStringLiteral("name"), q(c->name)},
                                           {QStringLiteral("input"), schemaObject(c->argumentsJson.empty()
                                                                                      ? std::string("{}")
                                                                                      : c->argumentsJson)}});
            }
        }
        if (!content.isEmpty())
            messages.append(QJsonObject{
                {QStringLiteral("role"),
                 message.author == app::Author::User ? QStringLiteral("user") : QStringLiteral("assistant")},
                {QStringLiteral("content"), content}});
    }
    QJsonObject body{{QStringLiteral("model"), q(request.model)},
                     {QStringLiteral("max_tokens"), request.maxOutputTokens.value_or(kDefaultMaxTokens)},
                     {QStringLiteral("messages"), messages},
                     {QStringLiteral("stream"), true}};
    if (!request.system.empty())
        body.insert(QStringLiteral("system"), q(request.system));
    QJsonArray tools;
    for (const app::ToolDefinition &tool : request.tools)
        tools.append(QJsonObject{{QStringLiteral("name"), q(tool.name)},
                                 {QStringLiteral("description"), q(tool.description)},
                                 {QStringLiteral("input_schema"), schemaObject(tool.parametersJson)}});
    if (!tools.isEmpty())
        body.insert(QStringLiteral("tools"), tools);
    return compact(body);
}

QByteArray geminiBody(const app::Request &request)
{
    std::map<std::string, std::string, std::less<>> callNames;
    QJsonArray contents;
    for (const app::Message &message : request.messages) {
        QJsonArray parts;
        forEach<app::ToolResult>(message, [&](const app::ToolResult &r) {
            QJsonObject response{{QStringLiteral("name"), q(callNames[r.callId])},
                                 {QStringLiteral("response"),
                                  QJsonObject{{r.isError ? QStringLiteral("error") : QStringLiteral("result"), q(r.content)}}}};
            if (!r.callId.empty())
                response.insert(QStringLiteral("id"), q(r.callId));
            parts.append(QJsonObject{{QStringLiteral("functionResponse"), response}});
        });
        for (const app::Part &part : message.parts) {
            if (const auto *t = std::get_if<app::Text>(&part)) {
                if (!t->text.empty())
                    parts.append(QJsonObject{{QStringLiteral("text"), q(t->text)}});
            } else if (const auto *i = std::get_if<app::Image>(&part)) {
                parts.append(QJsonObject{{QStringLiteral("inlineData"),
                                          QJsonObject{{QStringLiteral("mimeType"), q(i->mediaType)},
                                                      {QStringLiteral("data"), q(i->base64)}}}});
            } else if (const auto *c = std::get_if<app::ToolCall>(&part)) {
                callNames[c->id] = c->name;
                QJsonObject call{{QStringLiteral("name"), q(c->name)},
                                 {QStringLiteral("args"), argumentsObject(c->argumentsJson)}};
                if (!c->id.empty())
                    call.insert(QStringLiteral("id"), q(c->id));
                QJsonObject p{{QStringLiteral("functionCall"), call}};
                if (!c->signature.empty())
                    p.insert(QStringLiteral("thoughtSignature"), q(c->signature));
                parts.append(p);
            }
        }
        if (!parts.isEmpty())
            contents.append(QJsonObject{
                {QStringLiteral("role"),
                 message.author == app::Author::User ? QStringLiteral("user") : QStringLiteral("model")},
                {QStringLiteral("parts"), parts}});
    }
    QJsonObject body{{QStringLiteral("contents"), contents}};
    if (!request.system.empty())
        body.insert(QStringLiteral("systemInstruction"),
                    QJsonObject{{QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), q(request.system)}}}}});
    QJsonArray declarations;
    for (const app::ToolDefinition &tool : request.tools)
        declarations.append(QJsonObject{{QStringLiteral("name"), q(tool.name)},
                                        {QStringLiteral("description"), q(tool.description)},
                                        {QStringLiteral("parametersJsonSchema"), schemaObject(tool.parametersJson)}});
    if (!declarations.isEmpty())
        body.insert(QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("functionDeclarations"), declarations}}});
    if (request.maxOutputTokens)
        body.insert(QStringLiteral("generationConfig"),
                    QJsonObject{{QStringLiteral("maxOutputTokens"), *request.maxOutputTokens}});
    return compact(body);
}

app::ErrorKind kindOfStatus(int status)
{
    if (status == 401 || status == 403)
        return app::ErrorKind::Unauthorized;
    if (status == 429)
        return app::ErrorKind::RateLimited;
    if (status >= 400 && status < 500)
        return app::ErrorKind::Rejected;
    return app::ErrorKind::Server;
}

class ChatDecoder final : public StreamDecoder {
protected:
    bool finishOnClose() const override { return m_stop.has_value(); }

    void decode(const SseParser::Message &message) override
    {
        if (message.data == "[DONE]") {
            finish(m_stop.value_or(app::StopReason::End));
            return;
        }
        QJsonParseError parseError;
        const QJsonObject o = QJsonDocument::fromJson(message.data, &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            fail(app::ErrorKind::Malformed, parseError.errorString());
            return;
        }
        if (o.contains(QStringLiteral("error"))) {
            const QJsonObject e = o.value(QStringLiteral("error")).toObject();
            const int code = e.value(QStringLiteral("code")).toInt();
            fail(code ? kindOfStatus(code) : app::ErrorKind::Server, e.value(QStringLiteral("message")).toString());
            return;
        }
        if (const QJsonObject u = o.value(QStringLiteral("usage")).toObject(); !u.isEmpty()) {
            m_usage.inputTokens = u.value(QStringLiteral("prompt_tokens")).toInteger();
            m_usage.outputTokens = u.value(QStringLiteral("completion_tokens")).toInteger();
            m_usage.cachedInputTokens =
                u.value(QStringLiteral("prompt_tokens_details")).toObject().value(QStringLiteral("cached_tokens")).toInteger();
            if (u.value(QStringLiteral("cost")).isDouble())
                m_usage.reportedCost = u.value(QStringLiteral("cost")).toDouble();
        }
        const QJsonObject choice = o.value(QStringLiteral("choices")).toArray().first().toObject();
        const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
        if (const QString content = delta.value(QStringLiteral("content")).toString(); !content.isEmpty())
            text(content);
        for (const QJsonValue &v : delta.value(QStringLiteral("tool_calls")).toArray()) {
            const QJsonObject call = v.toObject();
            const int index = call.value(QStringLiteral("index")).toInt();
            const QJsonObject function = call.value(QStringLiteral("function")).toObject();
            if (call.contains(QStringLiteral("id")) || function.contains(QStringLiteral("name")))
                startCall(index, call.value(QStringLiteral("id")).toString(),
                          function.value(QStringLiteral("name")).toString());
            if (const QString args = function.value(QStringLiteral("arguments")).toString(); !args.isEmpty())
                callArguments(index, args);
        }
        const QString reason = choice.value(QStringLiteral("finish_reason")).toString();
        if (reason == QLatin1String("stop"))
            m_stop = app::StopReason::End;
        else if (reason == QLatin1String("tool_calls") || reason == QLatin1String("function_call"))
            m_stop = app::StopReason::ToolUse;
        else if (reason == QLatin1String("length"))
            m_stop = app::StopReason::MaxTokens;
        else if (reason == QLatin1String("content_filter"))
            m_stop = app::StopReason::Refused;
    }
};

class ResponsesDecoder final : public StreamDecoder {
protected:
    void decode(const SseParser::Message &message) override
    {
        QJsonParseError parseError;
        const QJsonObject o = QJsonDocument::fromJson(message.data, &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            fail(app::ErrorKind::Malformed, parseError.errorString());
            return;
        }
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("response.output_text.delta")) {
            text(o.value(QStringLiteral("delta")).toString());
        } else if (type == QLatin1String("response.output_item.added")) {
            const QJsonObject item = o.value(QStringLiteral("item")).toObject();
            if (item.value(QStringLiteral("type")).toString() == QLatin1String("function_call")) {
                m_hasCalls = true;
                startCall(o.value(QStringLiteral("output_index")).toInt(), item.value(QStringLiteral("call_id")).toString(),
                          item.value(QStringLiteral("name")).toString());
            }
        } else if (type == QLatin1String("response.function_call_arguments.delta")) {
            callArguments(o.value(QStringLiteral("output_index")).toInt(), o.value(QStringLiteral("delta")).toString());
        } else if (type == QLatin1String("response.completed") || type == QLatin1String("response.incomplete")) {
            const QJsonObject response = o.value(QStringLiteral("response")).toObject();
            const QJsonObject u = response.value(QStringLiteral("usage")).toObject();
            m_usage.inputTokens = u.value(QStringLiteral("input_tokens")).toInteger();
            m_usage.outputTokens = u.value(QStringLiteral("output_tokens")).toInteger();
            m_usage.cachedInputTokens =
                u.value(QStringLiteral("input_tokens_details")).toObject().value(QStringLiteral("cached_tokens")).toInteger();
            const QString incomplete =
                response.value(QStringLiteral("incomplete_details")).toObject().value(QStringLiteral("reason")).toString();
            if (incomplete == QLatin1String("max_output_tokens"))
                finish(app::StopReason::MaxTokens);
            else if (incomplete == QLatin1String("content_filter"))
                finish(app::StopReason::Refused);
            else
                finish(m_hasCalls ? app::StopReason::ToolUse : app::StopReason::End);
        } else if (type == QLatin1String("response.failed")) {
            const QJsonObject e = o.value(QStringLiteral("response")).toObject().value(QStringLiteral("error")).toObject();
            fail(app::ErrorKind::Server, e.value(QStringLiteral("message")).toString());
        } else if (type == QLatin1String("error")) {
            const QString code = o.value(QStringLiteral("code")).toString();
            const bool limited = code.contains(QLatin1String("rate_limit")) || code.contains(QLatin1String("usage_limit"));
            fail(limited ? app::ErrorKind::RateLimited : app::ErrorKind::Server, o.value(QStringLiteral("message")).toString());
        }
    }

private:
    bool m_hasCalls = false;
};

class AnthropicDecoder final : public StreamDecoder {
protected:
    void decode(const SseParser::Message &message) override
    {
        QJsonParseError parseError;
        const QJsonObject o = QJsonDocument::fromJson(message.data, &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            fail(app::ErrorKind::Malformed, parseError.errorString());
            return;
        }
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("message_start")) {
            readUsage(o.value(QStringLiteral("message")).toObject().value(QStringLiteral("usage")).toObject());
        } else if (type == QLatin1String("content_block_start")) {
            const QJsonObject block = o.value(QStringLiteral("content_block")).toObject();
            const QString kind = block.value(QStringLiteral("type")).toString();
            if (kind == QLatin1String("tool_use"))
                startCall(o.value(QStringLiteral("index")).toInt(), block.value(QStringLiteral("id")).toString(),
                          block.value(QStringLiteral("name")).toString());
            else if (kind == QLatin1String("text") && !block.value(QStringLiteral("text")).toString().isEmpty())
                text(block.value(QStringLiteral("text")).toString());
        } else if (type == QLatin1String("content_block_delta")) {
            const QJsonObject delta = o.value(QStringLiteral("delta")).toObject();
            const QString kind = delta.value(QStringLiteral("type")).toString();
            if (kind == QLatin1String("text_delta"))
                text(delta.value(QStringLiteral("text")).toString());
            else if (kind == QLatin1String("input_json_delta"))
                callArguments(o.value(QStringLiteral("index")).toInt(), delta.value(QStringLiteral("partial_json")).toString());
        } else if (type == QLatin1String("message_delta")) {
            readUsage(o.value(QStringLiteral("usage")).toObject());
            const QString reason = o.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString();
            if (reason == QLatin1String("tool_use"))
                m_stop = app::StopReason::ToolUse;
            else if (reason == QLatin1String("max_tokens"))
                m_stop = app::StopReason::MaxTokens;
            else if (reason == QLatin1String("refusal"))
                m_stop = app::StopReason::Refused;
            else if (!reason.isEmpty())
                m_stop = app::StopReason::End;
        } else if (type == QLatin1String("message_stop")) {
            finish(m_stop.value_or(app::StopReason::End));
        } else if (type == QLatin1String("error")) {
            const QJsonObject e = o.value(QStringLiteral("error")).toObject();
            const QString kind = e.value(QStringLiteral("type")).toString();
            app::ErrorKind error = app::ErrorKind::Server;
            if (kind == QLatin1String("rate_limit_error"))
                error = app::ErrorKind::RateLimited;
            else if (kind == QLatin1String("authentication_error") || kind == QLatin1String("permission_error"))
                error = app::ErrorKind::Unauthorized;
            else if (kind == QLatin1String("invalid_request_error"))
                error = app::ErrorKind::Rejected;
            fail(error, e.value(QStringLiteral("message")).toString());
        }
    }

private:
    void readUsage(const QJsonObject &u)
    {
        if (u.contains(QStringLiteral("input_tokens"))) {
            const qint64 read = u.value(QStringLiteral("cache_read_input_tokens")).toInteger();
            m_usage.inputTokens = u.value(QStringLiteral("input_tokens")).toInteger() + read +
                                  u.value(QStringLiteral("cache_creation_input_tokens")).toInteger();
            m_usage.cachedInputTokens = read;
        }
        if (u.contains(QStringLiteral("output_tokens")))
            m_usage.outputTokens = u.value(QStringLiteral("output_tokens")).toInteger();
    }
};

class GeminiDecoder final : public StreamDecoder {
protected:
    bool finishOnClose() const override { return m_stop.has_value(); }

    void decode(const SseParser::Message &message) override
    {
        QJsonParseError parseError;
        const QJsonObject o = QJsonDocument::fromJson(message.data, &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            fail(app::ErrorKind::Malformed, parseError.errorString());
            return;
        }
        if (o.contains(QStringLiteral("error"))) {
            const QJsonObject e = o.value(QStringLiteral("error")).toObject();
            const int code = e.value(QStringLiteral("code")).toInt();
            fail(code ? kindOfStatus(code) : app::ErrorKind::Server, e.value(QStringLiteral("message")).toString());
            return;
        }
        if (const QJsonObject u = o.value(QStringLiteral("usageMetadata")).toObject(); !u.isEmpty()) {
            m_usage.inputTokens = u.value(QStringLiteral("promptTokenCount")).toInteger() +
                                  u.value(QStringLiteral("toolUsePromptTokenCount")).toInteger();
            m_usage.outputTokens = u.value(QStringLiteral("candidatesTokenCount")).toInteger() +
                                   u.value(QStringLiteral("thoughtsTokenCount")).toInteger();
            m_usage.cachedInputTokens = u.value(QStringLiteral("cachedContentTokenCount")).toInteger();
        }
        if (!o.value(QStringLiteral("promptFeedback")).toObject().value(QStringLiteral("blockReason")).toString().isEmpty()) {
            finish(app::StopReason::Refused);
            return;
        }
        const QJsonObject candidate = o.value(QStringLiteral("candidates")).toArray().first().toObject();
        for (const QJsonValue &v : candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray()) {
            const QJsonObject part = v.toObject();
            if (part.value(QStringLiteral("thought")).toBool())
                continue;
            if (part.contains(QStringLiteral("functionCall"))) {
                const QJsonObject call = part.value(QStringLiteral("functionCall")).toObject();
                const int index = m_callCount++;
                QString id = call.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    id = QStringLiteral("call_%1").arg(index);
                startCall(index, id, call.value(QStringLiteral("name")).toString());
                callArguments(index, QString::fromUtf8(compact(call.value(QStringLiteral("args")).toObject())));
                callSignature(index, part.value(QStringLiteral("thoughtSignature")).toString());
            } else {
                text(part.value(QStringLiteral("text")).toString());
            }
        }
        const QString reason = candidate.value(QStringLiteral("finishReason")).toString();
        if (reason.isEmpty())
            return;
        const QString detail = candidate.value(QStringLiteral("finishMessage")).toString();
        static const QStringList refused{
            QStringLiteral("SAFETY"),       QStringLiteral("RECITATION"),         QStringLiteral("LANGUAGE"),
            QStringLiteral("BLOCKLIST"),    QStringLiteral("PROHIBITED_CONTENT"), QStringLiteral("SPII"),
            QStringLiteral("IMAGE_SAFETY"), QStringLiteral("IMAGE_PROHIBITED_CONTENT"),
            QStringLiteral("IMAGE_RECITATION"), QStringLiteral("ESCALATION")};
        static const QStringList malformed{QStringLiteral("MALFORMED_FUNCTION_CALL"), QStringLiteral("UNEXPECTED_TOOL_CALL"),
                                           QStringLiteral("TOO_MANY_TOOL_CALLS"), QStringLiteral("MALFORMED_RESPONSE")};
        if (reason == QLatin1String("MAX_TOKENS") || reason == QLatin1String("CONTINUATION"))
            m_stop = app::StopReason::MaxTokens;
        else if (refused.contains(reason))
            m_stop = app::StopReason::Refused;
        else if (malformed.contains(reason))
            fail(app::ErrorKind::Malformed, detail.isEmpty() ? reason : detail);
        else if (reason == QLatin1String("MISSING_THOUGHT_SIGNATURE"))
            fail(app::ErrorKind::Rejected, detail.isEmpty() ? reason : detail);
        else if (reason == QLatin1String("PUP_LIMITED_DISABLED"))
            fail(app::ErrorKind::Unauthorized, detail.isEmpty() ? reason : detail);
        else
            m_stop = m_callCount > 0 ? app::StopReason::ToolUse : app::StopReason::End;
    }

private:
    int m_callCount = 0;
};

std::vector<app::Model> geminiModels(const QJsonObject &root)
{
    std::vector<app::Model> models;
    for (const QJsonValue &v : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("supportedGenerationMethods")).toArray().contains(QStringLiteral("generateContent")))
            continue;
        QString id = o.value(QStringLiteral("name")).toString();
        if (id.startsWith(QLatin1String("models/")))
            id.remove(0, 7);
        if (id.isEmpty())
            continue;
        app::Model model;
        model.id = id.toStdString();
        const QString name = o.value(QStringLiteral("displayName")).toString();
        model.name = name.isEmpty() ? model.id : name.toStdString();
        if (o.value(QStringLiteral("inputTokenLimit")).isDouble())
            model.contextTokens = o.value(QStringLiteral("inputTokenLimit")).toInteger();
        models.push_back(std::move(model));
    }
    return models;
}

}

QList<SseParser::Message> SseParser::feed(const QByteArray &bytes)
{
    m_buffer += bytes;
    if (!m_started) {
        const QByteArray bom("\xEF\xBB\xBF");
        if (m_buffer.size() < bom.size() && bom.startsWith(m_buffer))
            return {};
        if (m_buffer.startsWith(bom))
            m_buffer.remove(0, bom.size());
        m_started = true;
    }
    QList<Message> out;
    qsizetype start = 0;
    for (;;) {
        qsizetype end = start;
        while (end < m_buffer.size() && m_buffer[end] != '\n' && m_buffer[end] != '\r')
            ++end;
        if (end == m_buffer.size())
            break;
        qsizetype next = end + 1;
        if (m_buffer[end] == '\r') {
            if (next == m_buffer.size())
                break;
            if (m_buffer[next] == '\n')
                ++next;
        }
        const QByteArray line = m_buffer.mid(start, end - start);
        start = next;
        if (line.isEmpty()) {
            if (m_hasData) {
                if (m_data.endsWith('\n'))
                    m_data.chop(1);
                out.append({m_event.isEmpty() ? QByteArray("message") : m_event, m_data});
            }
            m_event.clear();
            m_data.clear();
            m_hasData = false;
            continue;
        }
        if (line.startsWith(':'))
            continue;
        const qsizetype colon = line.indexOf(':');
        const QByteArray field = colon < 0 ? line : line.left(colon);
        QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
        if (value.startsWith(' '))
            value.remove(0, 1);
        if (field == "event") {
            m_event = value;
        } else if (field == "data") {
            m_data += value;
            m_data += '\n';
            m_hasData = true;
        }
    }
    m_buffer.remove(0, start);
    return out;
}

HttpCall encodeRequest(const app::Provider &provider, const QByteArray &key, const app::Request &request)
{
    HttpCall call;
    call.headers = authHeaders(provider, key);
    call.headers.emplace_back("Content-Type", "application/json");
    call.headers.emplace_back("Accept", "text/event-stream");
    switch (provider.wire) {
    case app::Wire::ChatCompletions:
        call.path = QStringLiteral("/chat/completions");
        call.body = chatBody(provider, request);
        break;
    case app::Wire::Responses:
        call.path = QStringLiteral("/responses");
        call.body = responsesBody(request);
        break;
    case app::Wire::AnthropicMessages:
        call.path = QStringLiteral("/messages");
        call.body = anthropicBody(request);
        break;
    case app::Wire::Gemini:
        call.path = QStringLiteral("/models/%1:streamGenerateContent?alt=sse")
                        .arg(QString::fromLatin1(QUrl::toPercentEncoding(q(request.model))));
        call.body = geminiBody(request);
        break;
    }
    return call;
}

HttpCall modelsCall(const app::Provider &provider, const QByteArray &key)
{
    HttpCall call;
    call.headers = authHeaders(provider, key);
    switch (provider.wire) {
    case app::Wire::AnthropicMessages:
        call.path = QStringLiteral("/models?limit=1000");
        break;
    case app::Wire::Gemini:
        call.path = QStringLiteral("/models?pageSize=1000");
        break;
    case app::Wire::ChatCompletions:
    case app::Wire::Responses:
        call.path = QStringLiteral("/models");
        break;
    }
    return call;
}

std::vector<app::Model> decodeModels(app::Wire wire, const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (wire == app::Wire::Gemini)
        return geminiModels(root);
    std::vector<app::Model> models;
    for (const QJsonValue &v : root.value(QStringLiteral("data")).toArray()) {
        const QJsonObject o = v.toObject();
        app::Model model;
        model.id = o.value(QStringLiteral("id")).toString().toStdString();
        if (model.id.empty())
            continue;
        QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = o.value(QStringLiteral("display_name")).toString();
        model.name = name.isEmpty() ? model.id : name.toStdString();
        if (o.value(QStringLiteral("context_length")).isDouble())
            model.contextTokens = o.value(QStringLiteral("context_length")).toInteger();
        else if (o.value(QStringLiteral("max_input_tokens")).isDouble())
            model.contextTokens = o.value(QStringLiteral("max_input_tokens")).toInteger();
        const QJsonValue modalities =
            o.value(QStringLiteral("architecture")).toObject().value(QStringLiteral("input_modalities"));
        const QJsonValue parameters = o.value(QStringLiteral("supported_parameters"));
        if (modalities.isArray() || parameters.isArray())
            model.capabilities = app::Capabilities{
                .tools = parameters.toArray().contains(QStringLiteral("tools")),
                .vision = modalities.toArray().contains(QStringLiteral("image")),
            };
        const QJsonObject pricing = o.value(QStringLiteral("pricing")).toObject();
        if (pricing.contains(QStringLiteral("prompt")) && pricing.contains(QStringLiteral("completion"))) {
            const auto perMillion = [&](const QString &k) { return pricing.value(k).toString().toDouble() * 1e6; };
            app::Price price{.input = perMillion(QStringLiteral("prompt")),
                             .output = perMillion(QStringLiteral("completion"))};
            if (pricing.contains(QStringLiteral("input_cache_read")))
                price.cachedInput = perMillion(QStringLiteral("input_cache_read"));
            model.price = price;
        }
        models.push_back(std::move(model));
    }
    return models;
}

QString errorDetail(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QJsonValue error = o.value(QStringLiteral("error"));
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    if (error.isString())
        return error.toString();
    return o.value(QStringLiteral("message")).toString();
}

std::vector<app::Event> StreamDecoder::feed(const SseParser::Message &message)
{
    if (!m_done)
        decode(message);
    return std::exchange(m_out, {});
}

std::vector<app::Event> StreamDecoder::close()
{
    if (!m_done) {
        if (finishOnClose())
            finish(*m_stop);
        else
            fail(app::ErrorKind::Malformed, QStringLiteral("the stream ended before the response finished"));
    }
    return std::exchange(m_out, {});
}

void StreamDecoder::text(const QString &delta)
{
    if (delta.isEmpty())
        return;
    const std::string s = delta.toStdString();
    m_text += s;
    m_out.emplace_back(app::TextDelta{s});
}

void StreamDecoder::startCall(int index, const QString &id, const QString &name)
{
    app::ToolCall &call = m_calls[index];
    call.id = id.toStdString();
    call.name = name.toStdString();
    m_out.emplace_back(app::ToolCallStarted{index, call.id, call.name});
}

void StreamDecoder::callArguments(int index, const QString &delta)
{
    const std::string s = delta.toStdString();
    m_calls[index].argumentsJson += s;
    m_out.emplace_back(app::ToolCallArguments{index, s});
}

void StreamDecoder::callSignature(int index, const QString &signature)
{
    m_calls[index].signature = signature.toStdString();
}

void StreamDecoder::finish(app::StopReason reason)
{
    if (m_done)
        return;
    m_done = true;
    app::Finished finished{.reason = reason, .text = m_text, .toolCalls = {}, .usage = m_usage};
    for (auto &[index, call] : m_calls) {
        if (call.argumentsJson.empty())
            call.argumentsJson = "{}";
        finished.toolCalls.push_back(call);
    }
    m_out.emplace_back(std::move(finished));
}

void StreamDecoder::fail(app::ErrorKind kind, const QString &detail)
{
    if (m_done)
        return;
    m_done = true;
    m_out.emplace_back(app::Failed{{.kind = kind, .httpStatus = 0, .detail = detail.toStdString()}});
}

std::unique_ptr<StreamDecoder> makeDecoder(app::Wire wire)
{
    switch (wire) {
    case app::Wire::ChatCompletions:
        return std::make_unique<ChatDecoder>();
    case app::Wire::Responses:
        return std::make_unique<ResponsesDecoder>();
    case app::Wire::AnthropicMessages:
        return std::make_unique<AnthropicDecoder>();
    case app::Wire::Gemini:
        return std::make_unique<GeminiDecoder>();
    }
    return nullptr;
}

app::ErrorKind errorKindOfStatus(int status) { return kindOfStatus(status); }

}
