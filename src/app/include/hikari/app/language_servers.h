#pragma once

#include "hikari/app/language_providers.h"
#include "hikari/app/plugin_runtime.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/problems.h"
#include "hikari/backends/lsp_connection.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <boost/dynamic_bitset.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <string>
#include <utility>
#include <vector>

namespace hikari::app {

// One field of a Document as a language server sees it: LSP line n is the n-th Line.
struct ServerText {
    QString text;
    std::vector<core::LineId> lines;
    std::vector<int> lengths; // UTF-16; 0 for a Comment Line
    boost::dynamic_bitset<> comments;
    std::vector<std::u8string> fields;
};

// Each override block, \N, \n and \h as as many spaces, so offsets still match.
QString blankTags(const QString &text);
// The UTF-16 ranges blankTags blanks, in order.
std::vector<std::pair<int, int>> blankedRanges(const QString &text);
ServerText serverText(const core::Document &document, application::ProblemField field, plugins::LanguageServer::Tags tags);
ServerText lineServerText(const core::LineRecord &line, application::ProblemField field, plugins::LanguageServer::Tags tags);
// The diagnostics on Lines that aren't Comments, as problems; a range running past its Line ends with the Line.
std::vector<application::Problem> diagnosticProblems(const QJsonArray &diagnostics, const ServerText &text,
                                                     application::ProblemField field);
// A codeAction answer's item as a fix for LSP line `line` of `text`: a quickfix whose edits all stay inside that
// Line and touch no blanked range, or nothing.
std::optional<application::ProblemFix> quickFix(const QJsonObject &action, const QString &uri, const ServerText &text, int line,
                                                plugins::LanguageServer::Tags tags);
// A completion answer as items for LSP line `line`, holding `text`, asked at `offset`.
std::vector<CompletionItem> serverCompletions(const QJsonValue &answer, int line, const QString &text, int offset, const QString &source);
std::optional<HoverText> serverHover(const QJsonValue &answer, int line, const QString &text, int offset, const QString &source);

// The plugins' contributes.languageServers (#1227): HikariSub runs each one, keeps its texts current, shows its
// diagnostics as problems of the source "plugin:<plugin id>/<server id>" with its quick fixes, and asks it for
// completions and hovers.
class LanguageServers : public QObject {
    Q_OBJECT
public:
    using Open = std::vector<std::pair<application::DocumentId, const application::EditSession *>>;
    using Editing = std::optional<std::pair<application::DocumentId, core::LineId>>;
    struct Wiring {
        std::function<std::vector<PluginRuntime::LanguageServer>()> servers;
        std::function<std::expected<backends::ProgramProcess::Options, QString>(const std::string &plugin,
                                                                                const std::string &server)>
            start;
        std::function<api::Json(const std::string &plugin)> settings;
        std::function<void(const std::string &plugin, const QString &line)> log;
        std::function<Open()> open;
        // The line editor's Line, for servers of scope line.
        std::function<Editing()> editing;
        application::Problems *problems = nullptr;
    };
    struct Limits {
        int syncMs = 250;
        int restarts = 3;
        int restartWindowMs = 5 * 60 * 1000;
        int initializeMs = 30'000;
        int answerMs = 1'000;
        int codeActionMs = 5'000;
        std::size_t mostCodeActionLines = 1'000;
        backends::LspConnection::Limits connection;
    };
    enum class State { Idle, Starting, Running, Stopped };
    struct Stopped {
        std::string server;
        QString title;
        QString reason;
    };

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
    std::vector<Stopped> stopped(const std::string &plugin) const;

    // The trigger characters of every running server that completes `field`.
    QStringList triggers(const QString &field) const;
    bool hovers(const QString &field) const;
    // `done` gets every server's items in install order once each has answered, failed or run out of time.
    void complete(const LanguageProviders::Request &request, LanguageProviders::Completions done);
    // `done` gets the first server's hover in install order.
    void hover(const LanguageProviders::Request &request, LanguageProviders::Hover done);

signals:
    void statesChanged();

private:
    struct Text {
        std::uint64_t document = 0;
        std::uint64_t line = 0; // scope line only
        application::ProblemField field = application::ProblemField::Text;
        std::optional<std::uint64_t> revision;
        ServerText pending;
        ServerText sent;
        qint64 sentVersion = -1;
        std::uint64_t published = 0;
        bool opened = false;
        bool dirty = true;
    };
    using TextKey = std::tuple<std::uint64_t, std::uint64_t, int>;
    struct Server {
        PluginRuntime::LanguageServer entry;
        std::unique_ptr<backends::LspConnection> connection;
        State state = State::Idle;
        QString reason;
        std::deque<qint64> crashes;
        std::map<TextKey, Text> texts;
        QJsonObject capabilities;
        QTimer flush;
        QTimer initializeDeadline;
    };
    using Key = std::pair<std::string, std::string>;

    std::string source(const Server &server) const;
    bool serves(const Server &server, const QString &field, const QString &provider) const;
    // The server's open text for the request, and its LSP line.
    std::optional<std::pair<Text *, int>> textFor(Server &server, const LanguageProviders::Request &request);
    template<typename Answer>
    void ask(const QString &method, const QString &provider, const LanguageProviders::Request &request,
             std::function<Answer(const QJsonValue &, const Server &, int line)> read, std::function<void(std::vector<Answer>)> done);
    void syncDocuments(Server &server, const Open &open, bool &changed);
    void syncLines(Server &server, const Open &open, bool &changed);
    void setProblems(const Server &server, const Text &text, std::vector<application::Problem> problems);
    void requestFixes(Server &server, Text &text, const QJsonArray &diagnostics);
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
