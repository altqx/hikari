#include "hikari/backends/ai_file_key_store.h"
#include "hikari/backends/user_alone.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <windows.h>
#include <dpapi.h>

namespace hikari::backends::ai {

namespace {

namespace app = hikari::application::ai;

std::optional<QByteArray> dpapi(const QByteArray &data, bool seal)
{
    DATA_BLOB in{static_cast<DWORD>(data.size()), reinterpret_cast<BYTE *>(const_cast<char *>(data.data()))};
    DATA_BLOB out{};
    const BOOL done = seal ? CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                           : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!done)
        return std::nullopt;
    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

}

FileKeyStore::FileKeyStore(QString folder, std::unique_ptr<app::KeyStore> earlier)
    : m_folder(std::move(folder)), m_earlier(std::move(earlier))
{
}

QString FileKeyStore::path(const std::string &providerId) const
{
    return m_folder + QLatin1Char('/') + QString::fromLatin1(QByteArray::fromStdString(providerId).toHex()) +
           QStringLiteral(".key");
}

std::optional<app::KeyError> FileKeyStore::save(const std::string &providerId, const std::string &key)
{
    if (!QDir().mkpath(m_folder))
        return app::KeyError::Other;
    if (!keepForUserAlone(m_folder))
        return app::KeyError::NotPrivate;
    const std::optional<QByteArray> sealed = dpapi(QByteArray::fromStdString(key), true);
    if (!sealed)
        return app::KeyError::Other;
    QSaveFile file(path(providerId));
    if (!file.open(QIODevice::WriteOnly) || file.write(*sealed) != sealed->size() || !file.commit())
        return app::KeyError::Other;
    return std::nullopt;
}

void FileKeyStore::read(const std::string &providerId, std::function<void(app::KeyResult)> done)
{
    if (m_folder.isEmpty())
        return done(app::KeyError::Unavailable);
    QFile file(path(providerId));
    if (file.open(QIODevice::ReadOnly)) {
        const std::optional<QByteArray> key = dpapi(file.readAll(), false);
        return key ? done(key->toStdString()) : done(app::KeyError::Other);
    }
    if (file.exists())
        return done(app::KeyError::Denied);
    if (!m_earlier)
        return done(app::KeyError::NotFound);
    m_earlier->read(providerId, [this, alive = std::weak_ptr(m_alive), providerId, done](app::KeyResult result) {
        const auto *key = std::get_if<std::string>(&result);
        if (!alive.lock() || !key)
            return done(app::KeyError::NotFound);
        if (!save(providerId, *key))
            m_earlier->remove(providerId, [](std::optional<app::KeyError>) {});
        done(*key);
    });
}

void FileKeyStore::write(const std::string &providerId, const std::string &key,
                         std::function<void(std::optional<app::KeyError>)> done)
{
    if (m_folder.isEmpty())
        return done(app::KeyError::Unavailable);
    done(save(providerId, key));
}

void FileKeyStore::remove(const std::string &providerId, std::function<void(std::optional<app::KeyError>)> done)
{
    if (m_folder.isEmpty())
        return done(app::KeyError::Unavailable);
    if (m_earlier)
        m_earlier->remove(providerId, [](std::optional<app::KeyError>) {});
    QFile file(path(providerId));
    if (!file.exists())
        return done(app::KeyError::NotFound);
    done(file.remove() ? std::nullopt : std::optional(app::KeyError::Other));
}

}
