#pragma once

#include "hikari/backends/sandbox.h"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>

#include <memory>

namespace hikari::backends {

// One outside program in its sandbox profile. It never runs unconfined: start() refuses when the profile can't be
// enforced here. kill() ends the program and every process it started.
class ProgramProcess : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString program;
        QStringList arguments;
        QProcessEnvironment environment;
        QString workingDirectory;
        sandbox::Profile profile;
    };

    explicit ProgramProcess(Options options, QObject *parent = nullptr);
    ~ProgramProcess() override;

    // False with refusal() set when the sandbox can't apply; else the program starts, and failure comes as finished.
    bool start();
    QString refusal() const { return m_refusal; }
    void write(const QByteArray &bytes);
    void closeInput();
    void kill();
    bool running() const;
    qint64 processId() const { return m_pid; }

signals:
    void output(const QByteArray &bytes);
    void errorOutput(const QByteArray &bytes);
    // exitCode is -1 when it couldn't start or crashed; killed is true after kill().
    void finished(int exitCode, bool killed);

private:
    Options m_options;
    std::unique_ptr<sandbox::Launcher> m_launcher;
    std::unique_ptr<QProcess> m_process;
    QString m_refusal;
    qint64 m_pid = 0;
    bool m_killed = false;
    bool m_done = false;
    void end(int exitCode);
};

}
