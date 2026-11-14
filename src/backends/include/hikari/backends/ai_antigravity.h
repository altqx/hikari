#pragma once

#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/user_alone.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <string>

namespace hikari::backends::ai {

/// The keychain entry with HikariSub's own Google sign-in for Antigravity.
inline constexpr const char *kAntigravitySecret = "antigravity-google";
/// Where agy keeps its Google sign-in in a profile when it can't reach a keychain, as it can't in its sandbox.
inline constexpr const char *kAntigravityTokenFile = "gemini/app-data/antigravity-oauth-token";
/// The first agy with `agy acp`.
inline constexpr const char *kAntigravityMinimumVersion = "1.5.0";

/// Why the agent can't start when its sign-in wasn't saved for the user alone.
QString agentSignInNotSaved(SecretWrite result);

/// Lends a sign-in kept in the keychain as `secret` to the processes that read it from `file`. The first `acquire`
/// writes the file for the user only; after the last `release` a changed file goes back to the keychain, and the
/// file goes. A file left behind by a crash goes when the lease is made.
class TokenFileLease {
public:
    TokenFileLease(application::ai::KeyStore &keys, std::string secret, QString file, QString signInFirst);

    /// `done` gets an error when there is no sign-in to lend.
    void acquire(std::function<void(QString error)> done);
    void release();
    /// Moves the file a sign-in just wrote into the keychain.
    void keep(std::function<void(QString error)> done);
    const QString &file() const { return m_file; }

private:
    void giveBack();

    application::ai::KeyStore &m_keys;
    std::string m_secret;
    QString m_file;
    QString m_signInFirst;
    int m_users = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// Every path agy gets lies inside `profile`: its Gemini folder, its app data and its work folder.
QStringList antigravityArguments(const QString &profile);
/// `agy acp` in Hikari's own Antigravity profile, with that profile as its home so it never reads ~/.gemini.
AgentSpec antigravityAgent(const QString &program, const QString &profile, const QString &bridge,
                           std::shared_ptr<TokenFileLease> lease);

/// `agy acp --login` in Antigravity's profile: names Google's sign-in page, takes the code that page shows, and
/// keeps the sign-in in the keychain as soon as agy writes it, whether or not agy exits.
class AntigravitySignIn final : public QObject {
    Q_OBJECT
public:
    AntigravitySignIn(AgentSpec spec, std::shared_ptr<TokenFileLease> lease, QObject *parent = nullptr);
    ~AntigravitySignIn() override;

    void start();
    void enterCode(const QString &code);
    void cancel();
    bool running() const { return m_process != nullptr; }

    static constexpr int kTimeoutMs = 10 * 60 * 1000;
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

signals:
    void linkReady(QUrl url);
    void codeEntered();
    /// Empty `error` when the sign-in is in the keychain.
    void finished(QString error);

private:
    void read();
    void lookForSignIn();
    void keep();
    void killGroup();
    void exited(int code, QProcess::ExitStatus status);
    void finish(const QString &error);

    AgentSpec m_spec;
    std::shared_ptr<TokenFileLease> m_lease;
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<sandbox::Launcher> m_launcher;
    QByteArray m_output;
    QTimer m_timeout;
    QTimer m_signedIn;
    int m_timeoutMs = kTimeoutMs;
    bool m_linked = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
