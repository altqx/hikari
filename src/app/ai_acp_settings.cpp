#include "hikari/app/ai_acp_settings.h"

#include "hikari/app/ai_features.h"
#include "hikari/backends/ai_antigravity.h"
#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QDir>
#include <QRandomGenerator>

namespace hikari::app {

AiAcpSettings::AiAcpSettings(AiFeatures &features, QObject *parent) : QObject(parent), m_features(features)
{
    connect(&m_features, &AiFeatures::switched, this, [this] {
        reset();
        emit antigravityChanged();
    });
}

AiAcpSettings::~AiAcpSettings()
{
    *m_alive = false;
}

bool AiAcpSettings::available()
{
#ifdef Q_OS_WIN
    return false;
#else
    return true;
#endif
}

QVariantMap AiAcpSettings::antigravity() const
{
    return {{QStringLiteral("signedIn"), m_signedIn},
            {QStringLiteral("state"), m_state},
            {QStringLiteral("error"), m_error}};
}

void AiAcpSettings::reset()
{
    m_signIn.reset();
    m_signedIn = false;
    m_state.clear();
    m_error.clear();
    m_page.clear();
}

void AiAcpSettings::check()
{
    application::ai::KeyStore *keys = m_features.keys();
    if (!keys)
        return;
    keys->read(backends::ai::kAntigravitySecret, [this, alive = std::weak_ptr(m_alive)](application::ai::KeyResult result) {
        if (!alive.lock())
            return;
        const auto *token = std::get_if<std::string>(&result);
        m_signedIn = token && !token->empty();
        emit antigravityChanged();
    });
}

void AiAcpSettings::signIn()
{
    if (m_signIn)
        return;
    m_signIn = m_features.antigravitySignIn();
    m_error.clear();
    m_page.clear();
    if (!m_signIn) {
        m_error = QCoreApplication::translate("HikariSub.Options", "Antigravity isn’t on PATH.");
        return emit antigravityChanged();
    }
    connect(m_signIn.get(), &backends::ai::AntigravitySignIn::linkReady, this, [this](const QUrl &page) {
        m_page = page;
        m_state = QStringLiteral("code");
        openPage();
        emit antigravityChanged();
    });
    connect(m_signIn.get(), &backends::ai::AntigravitySignIn::codeEntered, this, [this] {
        m_state = QStringLiteral("checking");
        emit antigravityChanged();
    });
    connect(m_signIn.get(), &backends::ai::AntigravitySignIn::finished, this, &AiAcpSettings::finished);
    m_state = QStringLiteral("starting");
    emit antigravityChanged();
    m_signIn->start();
}

void AiAcpSettings::enterCode(const QString &code)
{
    if (m_signIn)
        m_signIn->enterCode(code);
}

void AiAcpSettings::cancel()
{
    if (m_signIn)
        m_signIn->cancel();
    m_error.clear();
    emit antigravityChanged();
}

void AiAcpSettings::finished(const QString &error)
{
    if (m_signIn)
        m_signIn.release()->deleteLater();
    m_state.clear();
    m_page.clear();
    m_error = error == QLatin1String("cancelled") ? QString() : error;
    if (error.isEmpty())
        m_signedIn = true;
    emit antigravityChanged();
}

void AiAcpSettings::openPage() const
{
    if (m_page.scheme() == QLatin1String("https") && m_page.host() == QLatin1String("accounts.google.com"))
        backends::desktop::openUrl(m_page);
}

void AiAcpSettings::signOut()
{
    application::ai::KeyStore *keys = m_features.keys();
    if (!keys)
        return;
    keys->remove(backends::ai::kAntigravitySecret, [this, alive = std::weak_ptr(m_alive)](auto error) {
        if (!alive.lock())
            return;
        if (!error)
            m_signedIn = false;
        emit antigravityChanged();
    });
}

QVariantMap AiAcpSettings::find(const QString &command) const
{
    const auto &acpCli = m_features.ports().acpCli;
    const QString path = acpCli && !command.trimmed().isEmpty() ? acpCli(command.trimmed()) : QString();
    return {{QStringLiteral("found"), !path.isEmpty()},
            {QStringLiteral("path"), QDir::toNativeSeparators(path)},
            {QStringLiteral("refused"), path.isEmpty() ? QString() : m_features.acpRefusal(path)}};
}

QString AiAcpSettings::newId()
{
    return QStringLiteral("custom-") + QString::number(QRandomGenerator::global()->generate64(), 36);
}

}
