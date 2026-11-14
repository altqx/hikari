#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace hikari::backends::ai {
class AntigravitySignIn;
}

namespace hikari::app {

class AiFeatures;

/// Settings › AI › External agents for the ACP agents: Antigravity's Google sign-in and the user's own agents.
class AiAcpSettings final : public QObject {
    Q_OBJECT
    /// {signedIn, state, error}; state is "", "starting", "code" or "checking".
    Q_PROPERTY(QVariantMap antigravity READ antigravity NOTIFY antigravityChanged FINAL)

public:
    explicit AiAcpSettings(AiFeatures &features, QObject *parent = nullptr);
    ~AiAcpSettings() override;

    QVariantMap antigravity() const;

    /// Reads whether HikariSub has its own Antigravity sign-in.
    Q_INVOKABLE void check();
    /// Runs `agy acp --login`, which opens Google's sign-in page.
    Q_INVOKABLE void signIn();
    /// The code Google's page shows once the user has signed in.
    Q_INVOKABLE void enterCode(const QString &code);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openPage() const;
    Q_INVOKABLE void signOut();
    /// {found, path} for a custom agent's command: a path, or a name looked up on PATH.
    Q_INVOKABLE QVariantMap find(const QString &command) const;
    /// A fresh id for a custom agent.
    Q_INVOKABLE static QString newId();

signals:
    void antigravityChanged();

private:
    void finished(const QString &error);
    void reset();

    AiFeatures &m_features;
    std::unique_ptr<backends::ai::AntigravitySignIn> m_signIn;
    bool m_signedIn = false;
    QString m_state;
    QString m_error;
    QUrl m_page;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
