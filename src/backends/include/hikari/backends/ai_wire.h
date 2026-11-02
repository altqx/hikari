#pragma once

#include "hikari/application/ai_providers.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <map>
#include <memory>
#include <vector>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;

/// Server-sent events, as the WHATWG HTML standard parses them.
class SseParser {
public:
    struct Message {
        QByteArray event;
        QByteArray data;
    };
    QList<Message> feed(const QByteArray &bytes);

private:
    QByteArray m_buffer;
    QByteArray m_event;
    QByteArray m_data;
    bool m_hasData = false;
    bool m_started = false;
};

struct HttpCall {
    QString path;
    QByteArray body;
    std::vector<std::pair<QByteArray, QByteArray>> headers;
};

HttpCall encodeRequest(const app::Provider &provider, const QByteArray &key, const app::Request &request);
HttpCall modelsCall(const app::Provider &provider, const QByteArray &key);
std::vector<app::Model> decodeModels(app::Wire wire, const QByteArray &body);
app::ErrorKind errorKindOfStatus(int status);
/// The provider's error message from an error body, when it has one.
QString errorDetail(const QByteArray &body);

/// Turns one provider's stream into events. done() is true after Finished or Failed.
class StreamDecoder {
public:
    virtual ~StreamDecoder() = default;
    std::vector<app::Event> feed(const SseParser::Message &message);
    /// The stream closed; a stream that never finished fails.
    std::vector<app::Event> close();
    bool done() const { return m_done; }

protected:
    virtual void decode(const SseParser::Message &message) = 0;
    virtual bool finishOnClose() const { return false; }

    void text(const QString &delta);
    void startCall(int index, const QString &id, const QString &name);
    void callArguments(int index, const QString &delta);
    void callSignature(int index, const QString &signature);
    void finish(app::StopReason reason);
    void fail(app::ErrorKind kind, const QString &detail);

    app::Usage m_usage;
    std::optional<app::StopReason> m_stop;

private:
    std::vector<app::Event> m_out;
    std::string m_text;
    std::map<int, app::ToolCall> m_calls;
    bool m_done = false;
};

std::unique_ptr<StreamDecoder> makeDecoder(app::Wire wire);

}
