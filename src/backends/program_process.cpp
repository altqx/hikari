#include "hikari/backends/program_process.h"

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif
#ifndef Q_OS_WIN
#include <signal.h>
#endif

namespace hikari::backends {

ProgramProcess::ProgramProcess(Options options, QObject *parent) : QObject(parent), m_options(std::move(options)) {}

ProgramProcess::~ProgramProcess()
{
    if (m_process)
        disconnect(m_process.get(), nullptr, this, nullptr);
    kill();
}

bool ProgramProcess::start()
{
    m_launcher = std::make_unique<sandbox::Launcher>(m_options.profile);
    if (const sandbox::Coverage coverage = m_launcher->coverage(); !coverage.complete()) {
        m_refusal = coverage.missing;
        return false;
    }
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_options.program);
    m_process->setArguments(m_options.arguments);
    m_process->setWorkingDirectory(m_options.workingDirectory);
    m_process->setProcessEnvironment(m_options.environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
#ifndef Q_OS_WIN
    m_process->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession |
                                        QProcess::UnixProcessFlag::CloseFileDescriptors |
                                        QProcess::UnixProcessFlag::ResetSignalHandlers);
#endif
#ifdef Q_OS_LINUX
    m_launcher->prepare(*m_process, [] { ::prctl(PR_SET_PDEATHSIG, SIGKILL); });
#else
    m_launcher->prepare(*m_process);
#endif
    connect(m_process.get(), &QProcess::started, this, [this] { m_pid = m_process->processId(); });
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this,
            [this] { emit output(m_process->readAllStandardOutput()); });
    connect(m_process.get(), &QProcess::readyReadStandardError, this,
            [this] { emit errorOutput(m_process->readAllStandardError()); });
    connect(m_process.get(), &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (const QByteArray rest = m_process->readAllStandardOutput(); !rest.isEmpty())
            emit output(rest);
        if (const QByteArray rest = m_process->readAllStandardError(); !rest.isEmpty())
            emit errorOutput(rest);
        end(status == QProcess::NormalExit ? code : -1);
    });
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            end(-1);
    });
    m_process->start();
    return true;
}

void ProgramProcess::end(int exitCode)
{
    if (std::exchange(m_done, true))
        return;
    emit finished(exitCode, m_killed);
}

void ProgramProcess::write(const QByteArray &bytes)
{
    if (running())
        m_process->write(bytes);
}

void ProgramProcess::closeInput()
{
    if (running())
        m_process->closeWriteChannel();
}

bool ProgramProcess::running() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void ProgramProcess::kill()
{
    if (!running())
        return;
    m_killed = true;
    if (m_launcher)
        m_launcher->killTree();
#ifndef Q_OS_WIN
    if (m_pid > 0)
        ::kill(-static_cast<pid_t>(m_pid), SIGKILL);
#endif
    m_process->kill();
}

}
