#pragma once

// GR1 (#364): harper-ls, Harper's language server, as a helper process spoken
// to over LSP on stdio (JSON-RPC with Content-Length frames). No Rust is
// linked into the app, and a crash of the checker ends only its process.
//
// Each check is a document of its own: didOpen with the text, its published
// diagnostics, then one codeAction request per diagnostic for its
// replacements, then didClose. harper-ls answers a code action for the lints
// at one position with its curated rules, so a finding takes the quick fixes
// whose range is its own. Positions are UTF-16 (LSP's default), as QString's.
//
// Settings are what workspace/configuration answers under "harper-ls". A
// change restarts the process: didChangeConfiguration makes harper-ls read
// its open documents again from disk, which these aren't on. The process
// starts with the first check after a start or a change.
//
// Runs on the thread that owns it, with a Qt event loop.

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <map>

namespace hikari::backends {

struct GrammarFinding {
    int start = 0; // UTF-16 [start, end) in the checked text
    int end = 0;
    QString rule; // Harper's rule (the diagnostic's code), e.g. "AnA"
    QString message;
    QStringList suggestions; // what its quick fixes put in, in Harper's order
    bool word = false;       // a spelling finding: a word that can be added to the dictionary
};

class HarperClient : public QObject {
    Q_OBJECT
public:
    // `program`: the harper-ls executable; `workingDirectory`: where it runs
    // (its workspace dictionary, which the settings point elsewhere).
    HarperClient(QString program, QString workingDirectory, QObject *parent = nullptr);
    ~HarperClient() override;

    // harper-ls's settings (dialect, linters, userDictPath, ...).
    void setSettings(const QJsonObject &settings);
    QJsonObject settings() const { return m_settings; }
    // Checks `text`; checked(ticket, findings) follows, or failed() when the
    // process ends before it. Starts the process when it isn't running.
    quint64 check(const QString &text);
    // The ticket's result is dropped (a newer text replaced it). A text
    // harper-ls has open stays in until it is done with it.
    void cancel(quint64 ticket);
    // Ends the process and drops every check (grammar check turned off).
    void stop();
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    // How many processes have been started (tests: none while it's off).
    int starts() const { return m_starts; }

signals:
    void checked(quint64 ticket, const QList<hikari::backends::GrammarFinding> &findings);
    void failed(const QString &message);

private:
    struct Check {
        QString text;
        QString uri;
        bool opened = false;
        bool cancelled = false; // open: finished without a result
        QList<GrammarFinding> findings;
        int actionsPending = 0;
    };
    void start();
    void send(const QJsonObject &message);
    void request(const QString &method, const QJsonObject &params, quint64 check, int finding);
    void notify(const QString &method, const QJsonObject &params);
    void openNext();
    void readOutput();
    void handle(const QJsonObject &message);
    void diagnostics(const QJsonObject &params);
    void actions(quint64 ticket, int finding, const QJsonValue &result);
    void finish(quint64 ticket);
    void lost(const QString &message);

    QString m_program;
    QString m_workingDirectory;
    QJsonObject m_settings;
    QProcess m_process;
    QByteArray m_buffer;
    bool m_ready = false;
    int m_starts = 0;
    qint64 m_nextId = 1;
    quint64 m_nextTicket = 1;
    std::map<quint64, Check> m_checks;
    // A request's check and finding (-1: initialize).
    std::map<qint64, std::pair<quint64, int>> m_requests;
};

} // namespace hikari::backends

Q_DECLARE_METATYPE(hikari::backends::GrammarFinding)
