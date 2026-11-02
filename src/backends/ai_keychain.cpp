#include "hikari/backends/ai_keychain.h"

#include <qt6keychain/keychain.h>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif

namespace hikari::backends::ai {

namespace {

namespace app = hikari::application::ai;

std::optional<app::KeyError> keyError(QKeychain::Error error)
{
    switch (error) {
    case QKeychain::NoError:
        return std::nullopt;
    case QKeychain::EntryNotFound:
        return app::KeyError::NotFound;
    case QKeychain::AccessDenied:
    case QKeychain::AccessDeniedByUser:
        return app::KeyError::Denied;
    case QKeychain::NoBackendAvailable:
    case QKeychain::NotImplemented:
        return app::KeyError::Unavailable;
    case QKeychain::CouldNotDeleteEntry:
    case QKeychain::OtherError:
        break;
    }
    return app::KeyError::Other;
}

// QtKeychain reports a missing keyring daemon as OtherError, so look for one on the session bus first.
bool keyringReachable()
{
#ifdef Q_OS_LINUX
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    const QStringList activatable = bus.interface()->activatableServiceNames().value();
    for (const QString &name : {QStringLiteral("org.freedesktop.secrets"), QStringLiteral("org.kde.kwalletd6"),
                                QStringLiteral("org.kde.kwalletd5")})
        if (bus.interface()->isServiceRegistered(name).value() || activatable.contains(name))
            return true;
    return false;
#else
    return true;
#endif
}

template <typename Job>
Job *makeJob(const QString &service, const std::string &providerId)
{
    auto *job = new Job(service);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(QStringLiteral("ai-provider/") + QString::fromStdString(providerId));
    return job;
}

}

void KeychainKeyStore::read(const std::string &providerId, std::function<void(app::KeyResult)> done)
{
    if (!keyringReachable()) {
        done(app::KeyError::Unavailable);
        return;
    }
    auto *job = makeJob<QKeychain::ReadPasswordJob>(m_service, providerId);
    QObject::connect(job, &QKeychain::Job::finished, job, [done](QKeychain::Job *j) {
        if (const std::optional<app::KeyError> error = keyError(j->error()))
            done(*error);
        else
            done(static_cast<QKeychain::ReadPasswordJob *>(j)->textData().toStdString());
    });
    job->start();
}

void KeychainKeyStore::write(const std::string &providerId, const std::string &key,
                             std::function<void(std::optional<app::KeyError>)> done)
{
    if (!keyringReachable()) {
        done(app::KeyError::Unavailable);
        return;
    }
    auto *job = makeJob<QKeychain::WritePasswordJob>(m_service, providerId);
    job->setTextData(QString::fromStdString(key));
    QObject::connect(job, &QKeychain::Job::finished, job, [done](QKeychain::Job *j) { done(keyError(j->error())); });
    job->start();
}

void KeychainKeyStore::remove(const std::string &providerId, std::function<void(std::optional<app::KeyError>)> done)
{
    if (!keyringReachable()) {
        done(app::KeyError::Unavailable);
        return;
    }
    auto *job = makeJob<QKeychain::DeletePasswordJob>(m_service, providerId);
    QObject::connect(job, &QKeychain::Job::finished, job, [done](QKeychain::Job *j) { done(keyError(j->error())); });
    job->start();
}

}
