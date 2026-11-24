#pragma once

#include "hikari/app/plugin_runtime.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/problems.h"
#include "hikari/backends/lsp_connection.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::app {

// One field of a Document as a language server sees it: LSP line n is the n-th Line.
struct ServerText {
    QString text;
    std::vector<core::LineId> lines;
    std::vector<int> lengths; // UTF-16; 0 for a Comment Line
    std::vector<bool> comments;
    std::vector<std::u8string> fields;
};

// Each override block, \N, \n and \h as as many spaces, so offsets still match.
QString blankTags(const QString &text);
ServerText serverText(const core::Document &document, application::ProblemField field, plugins::LanguageServer::Tags tags);
// The diagnostics on Lines that aren't Comments, as problems; a range running past its Line ends with the Line.
std::vector<application::Problem> diagnosticProblems(const QJsonArray &diagnostics, const ServerText &text,
                                                     application::ProblemField field);

// The plugins' contributes.languageServers (#1227): HikariSub runs each one, keeps its texts current and shows its
// diagnostics as problems of the source "plugin:<plugin id>/<server id>".
class LanguageServers : public QObject {
    Q_OBJECT
public:
    using Open = std::vector<std::pair<application::DocumentId, const application::EditSession *>>;
    struct Wiring {
        std::function<std::vector<PluginRuntime::LanguageServer>()> servers;
        std::function<std::expected<backends::ProgramProcess::Options, QString>(const std::string &plugin,
                                                                                const std::string &server)>
            start;
        std::function<api::Json(const std::string &plugin)> settings;
        std::function<void(const std::string &plugin, const QString &line)> log;
        std::function<Open()> open;
        application::Problems *problems = nullptr;
    };
    struct Limits {
        int syncMs = 250;
        int restarts = 3;
        int restartWindowMs = 5 * 60 * 1000;
        int initializeMs = 30'000;
        backends::LspConnection::Limits connection;
    };
    enum class State { Idle, Starting, Running, Stopped };

    explicit LanguageServers(Wiring wiring, QObject *parent = nullptr);
    LanguageServers(Wiring wiring, Limits limits, QObject *parent = nullptr);
    ~LanguageServers() override;

    // Reads the servers again: a removed one shuts down, a renewed one starts afresh.
    void serversChanged();
    // Opens, changes and closes each server's texts to match the open Documents. A server starts at its first text.
    void sync();
    State state(const std::string &plugin, const std::string &server) const;
    QString stoppedReason(const std::string &plugin, const std::string &server) const;
    void startAgain(const std::string &plugin, const std::string &server);

signals:
    void statesChanged();

private:
    struct Text {
        std::uint64_t document = 0;
        application::ProblemField field = application::ProblemField::Text;
        std::optional<std::uint64_t> revision;
        ServerText pending;
        ServerText sent;
        qint64 sentVersion = -1;
        bool opened = false;
        bool dirty = true;
    };
    struct Server {
        PluginRuntime::LanguageServer entry;
        std::unique_ptr<backends::LspConnection> connection;
        State state = State::Idle;
        QString reason;
        std::deque<qint64> crashes;
        std::map<std::pair<std::uint64_t, int>, Text> texts;
        QTimer flush;
        QTimer initializeDeadline;
    };
    using Key = std::pair<std::string, std::string>;

    std::string source(const Server &server) const;
    void start(Server &server);
    void initialize(Server &server);
    void flush(Server &server);
    void close(Server &server, Text &text);
    void ended(Key key, bool crashed, const QString &why);
    void diagnostics(Server &server, const QJsonValue &params);
    std::expected<QJsonValue, backends::LspConnection::Failure> serve(const Server &server, const QString &method,
                                                                     const QJsonValue &params) const;
    void retire(Server &server);
    void setState(Server &server, State state, QString reason = {});
    void log(const Server &server, const QString &line) const;

    Wiring m_wiring;
    Limits m_limits;
    std::map<Key, std::unique_ptr<Server>> m_servers;
    std::vector<std::unique_ptr<backends::LspConnection>> m_retiring;
};

}
