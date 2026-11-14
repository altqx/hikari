#include "hikari/backends/ai_antigravity.h"

#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/user_alone.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <optional>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/types.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace hikari::backends::ai {

namespace app = application::ai;

namespace {

std::optional<QByteArray> readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

constexpr QFileDevice::Permissions kUserOnlyFolder = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                    QFileDevice::ExeOwner | QFileDevice::ReadUser |
                                                    QFileDevice::WriteUser | QFileDevice::ExeUser;

bool makeFolder(const QString &folder)
{
    const QFileInfo info(folder);
    if (info.isDir())
        return true;
    const QString parent = info.absolutePath();
    return (QFileInfo(parent).isDir() || QDir().mkpath(parent)) && QDir().mkdir(folder, kUserOnlyFolder);
}

SecretWrite makeForUserOnly(const QString &folder)
{
    if (!makeFolder(folder))
        return SecretWrite::Failed;
    return isForUserAlone(folder) || keepForUserAlone(folder) ? SecretWrite::Saved : SecretWrite::NotPrivate;
}

SecretWrite writeForUserOnly(const QString &path, const QByteArray &data)
{
    if (const SecretWrite folder = makeForUserOnly(QFileInfo(path).absolutePath()); folder != SecretWrite::Saved)
        return folder;
    const QString temporary = path + QStringLiteral(".new");
    QFile::remove(temporary);
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return SecretWrite::Failed;
    const bool written = file.write(data) == data.size() && file.flush();
    file.close();
    QFile::remove(path);
    if (written && QFile::rename(temporary, path))
        return SecretWrite::Saved;
    QFile::remove(temporary);
    return SecretWrite::Failed;
}

std::optional<QByteArray> stored(const app::KeyResult &result)
{
    const auto *text = std::get_if<std::string>(&result);
    if (!text || text->empty())
        return std::nullopt;
    return QByteArray::fromBase64(QByteArray::fromStdString(*text));
}

}

QString agentSignInNotSaved(SecretWrite result)
{
    if (result == SecretWrite::NotPrivate)
        return QStringLiteral("The agent's sign-in wasn't saved because its folder can't be made private to you, for "
                              "example on a FAT32 or exFAT drive.");
    return QStringLiteral("The sign-in couldn't be written for the agent");
}

TokenFileLease::TokenFileLease(app::KeyStore &keys, std::string secret, QString file, QString signInFirst)
    : m_keys(keys), m_secret(std::move(secret)), m_file(std::move(file)), m_signInFirst(std::move(signInFirst))
{
    QFile::remove(m_file);
}

void TokenFileLease::acquire(std::function<void(QString error)> done)
{
    if (m_users > 0) {
        ++m_users;
        return done({});
    }
    m_keys.read(m_secret, [this, alive = std::weak_ptr(m_alive), done = std::move(done)](app::KeyResult result) {
        if (!alive.lock())
            return;
        if (m_users > 0) {
            ++m_users;
            return done({});
        }
        const std::optional<QByteArray> token = stored(result);
        if (!token) {
            QFile::remove(m_file);
            return done(m_signInFirst);
        }
        if (readAll(m_file) != token) {
            const SecretWrite written = writeForUserOnly(m_file, *token);
            if (written != SecretWrite::Saved)
                return done(agentSignInNotSaved(written));
        }
        ++m_users;
        done({});
    });
}

void TokenFileLease::release()
{
    if (m_users > 0 && --m_users == 0)
        giveBack();
}

void TokenFileLease::giveBack()
{
    m_keys.read(m_secret, [this, alive = std::weak_ptr(m_alive)](app::KeyResult result) {
        if (!alive.lock() || m_users > 0)
            return;
        const std::optional<QByteArray> refreshed = readAll(m_file);
        QFile::remove(m_file);
        const std::optional<QByteArray> kept = stored(result);
        if (kept && refreshed && !refreshed->isEmpty() && refreshed != kept)
            m_keys.write(m_secret, refreshed->toBase64().toStdString(), [](std::optional<app::KeyError>) {});
    });
}

void TokenFileLease::keep(std::function<void(QString error)> done)
{
    const std::optional<QByteArray> token = readAll(m_file);
    if (m_users == 0)
        QFile::remove(m_file);
    if (!token || token->isEmpty())
        return done(QStringLiteral("The agent signed in but left no sign-in to keep"));
    m_keys.write(m_secret, token->toBase64().toStdString(),
                 [alive = std::weak_ptr(m_alive), done = std::move(done)](std::optional<app::KeyError> error) {
                     if (!alive.lock())
                         return;
                     if (error == app::KeyError::NotPrivate)
                         return done(agentSignInNotSaved(SecretWrite::NotPrivate));
                     done(error ? QStringLiteral("Google signed you in, but the keychain couldn't keep the sign-in. "
                                                 "Unlock your keychain, then sign in again.")
                                : QString());
                 });
}

QStringList antigravityArguments(const QString &profile)
{
    return {QStringLiteral("acp"),
            QStringLiteral("--gemini-dir"),
            QDir::toNativeSeparators(profile + QStringLiteral("/gemini")),
            QStringLiteral("--app-data-dir"),
            QStringLiteral("app-data"),
            QStringLiteral("--disable-telemetry"),
            QStringLiteral("--include-default-customizations=false")};
}

AgentSpec antigravityAgent(const QString &program, const QString &profile, const QString &bridge,
                           std::shared_ptr<TokenFileLease> lease)
{
    const QString home = profile + QStringLiteral("/home");
    for (const QString &folder : {profile, profile + QStringLiteral("/gemini"), profile + QStringLiteral("/gemini/app-data"), home})
        makeForUserOnly(folder);
    AgentSpec spec = acpAgent(program, antigravityArguments(profile), {profile}, profile + QStringLiteral("/work"), bridge);
    spec.profile->name = sandbox::profileName(sandbox::OwnProfile::Antigravity);
    spec.environment.insert(QStringLiteral("HOME"), QDir::toNativeSeparators(home));
    if (lease) {
        spec.before = [lease](std::function<void(QString)> go) { lease->acquire(std::move(go)); };
        spec.after = [lease] { lease->release(); };
    }
    return spec;
}

AntigravitySignIn::AntigravitySignIn(AgentSpec spec, std::shared_ptr<TokenFileLease> lease, QObject *parent)
    : QObject(parent), m_spec(std::move(spec)), m_lease(std::move(lease))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(QStringLiteral("The sign-in timed out")); });
    m_signedIn.setInterval(200);
    connect(&m_signedIn, &QTimer::timeout, this, &AntigravitySignIn::lookForSignIn);
}

AntigravitySignIn::~AntigravitySignIn()
{
    if (m_process) {
        killGroup();
        m_process->disconnect(this);
        QFile::remove(m_lease->file());
    }
}

void AntigravitySignIn::killGroup()
{
#ifdef Q_OS_WIN
    m_launcher->killTree();
#else
    if (const qint64 pid = m_process->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
}

void AntigravitySignIn::start()
{
    if (m_process)
        return;
    m_output.clear();
    m_linked = false;
    QFile::remove(m_lease->file());
    QProcessEnvironment environment = sidecarEnvironment(QProcessEnvironment::systemEnvironment());
    environment.insert(m_spec.environment);
    m_process = std::make_unique<QProcess>();
    m_launcher = std::make_unique<sandbox::Launcher>(m_spec.profile);
    m_process->setProgram(m_spec.program);
    m_process->setArguments(m_spec.arguments + QStringList{QStringLiteral("--login")});
    m_process->setWorkingDirectory(m_spec.workingDirectory);
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
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
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &AntigravitySignIn::read);
    connect(m_process.get(), &QProcess::finished, this, &AntigravitySignIn::exited);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(QStringLiteral("Antigravity didn't start"));
    });
    m_timeout.start(m_timeoutMs);
    m_process->start();
}

void AntigravitySignIn::read()
{
    m_output += m_process->readAllStandardOutput();
    if (m_linked)
        return;
    static const QRegularExpression link(QStringLiteral(R"(https://accounts\.google\.com/\S+)"));
    if (const QRegularExpressionMatch match = link.match(QString::fromUtf8(m_output)); match.hasMatch()) {
        m_linked = true;
        emit linkReady(QUrl(match.captured(0), QUrl::StrictMode));
    }
}

void AntigravitySignIn::enterCode(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (!m_process || trimmed.isEmpty() || trimmed.contains(u'\n'))
        return;
    m_process->write(trimmed.toUtf8() + '\n');
    m_signedIn.start();
    emit codeEntered();
}

void AntigravitySignIn::lookForSignIn()
{
    const std::optional<QByteArray> written = readAll(m_lease->file());
    if (!written || !QJsonDocument::fromJson(*written).isObject())
        return;
    killGroup();
    keep();
}

void AntigravitySignIn::exited(int code, QProcess::ExitStatus status)
{
    if (status != QProcess::NormalExit || code != 0) {
        const QStringList lines = QString::fromUtf8(m_output).split(u'\n', Qt::SkipEmptyParts);
        return finish(lines.isEmpty() ? QStringLiteral("Antigravity's sign-in failed") : lines.last().trimmed());
    }
    keep();
}

void AntigravitySignIn::keep()
{
    m_timeout.stop();
    m_signedIn.stop();
    std::unique_ptr<QProcess> done = std::move(m_process);
    done->disconnect(this);
    done.release()->deleteLater();
    m_lease->keep([this, alive = std::weak_ptr(m_alive)](QString error) {
        if (alive.lock())
            emit finished(error);
    });
}

void AntigravitySignIn::cancel()
{
    if (!m_process)
        return;
    killGroup();
    finish(QStringLiteral("cancelled"));
}

void AntigravitySignIn::finish(const QString &error)
{
    if (!m_process)
        return;
    m_timeout.stop();
    m_signedIn.stop();
    std::unique_ptr<QProcess> done = std::move(m_process);
    done->disconnect(this);
    if (done->state() != QProcess::NotRunning)
        done->kill();
    done.release()->deleteLater();
    QFile::remove(m_lease->file());
    emit finished(error);
}

}
