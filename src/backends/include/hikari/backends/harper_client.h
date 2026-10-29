#pragma once

// Runs on the thread that owns it, with a Qt event loop. Positions are UTF-16 (LSP's
// default).

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
    HarperClient(QString program, QString workingDirectory, QObject *parent = nullptr);
    ~HarperClient() override;

    // Keys as harper-ls's workspace/configuration (dialect, linters, userDictPath, ...).
    // Stops a running process, which the next check starts again: on didChangeConfiguration
    // harper-ls rereads open documents from disk, where these texts are not.
    void setSettings(const QJsonObject &settings);
    QJsonObject settings() const { return m_settings; }
    // checked(ticket, findings) follows, or failed() if the process ends first.
    quint64 check(const QString &text);
    // Drops the ticket's result; a text harper-ls has open stays open until it is done.
    void cancel(quint64 ticket);
    // Ends the process and drops every check.
    void stop();
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    int starts() const { return m_starts; }

signals:
    void checked(quint64 ticket, const QList<hikari::backends::GrammarFinding> &findings);
    void failed(const QString &message);

private:
    struct Check {
        QString text;
        QString uri;
        bool opened = false;
        bool cancelled = false;
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

}

Q_DECLARE_METATYPE(hikari::backends::GrammarFinding)
