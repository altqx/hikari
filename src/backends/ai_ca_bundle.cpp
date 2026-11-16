#include "hikari/backends/ai_ca_bundle.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace hikari::backends::ai {

bool writeCaBundle(const QString &path, const QList<QSslCertificate> &certificates)
{
    QSaveFile file(path);
    if (certificates.isEmpty() || !file.open(QIODevice::WriteOnly))
        return false;
    for (const QSslCertificate &certificate : certificates)
        if (file.write(certificate.toPem()) < 0)
            return false;
    return file.commit();
}

QList<QSslCertificate> userAndMachineRoots()
{
    QList<QSslCertificate> out;
#ifdef Q_OS_WIN
    QSet<QByteArray> seen;
    for (const DWORD location : {CERT_SYSTEM_STORE_CURRENT_USER, CERT_SYSTEM_STORE_LOCAL_MACHINE}) {
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                         location | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, L"ROOT");
        if (!store)
            continue;
        for (PCCERT_CONTEXT context = CertEnumCertificatesInStore(store, nullptr); context;
             context = CertEnumCertificatesInStore(store, context)) {
            const QByteArray der(reinterpret_cast<const char *>(context->pbCertEncoded),
                                 static_cast<qsizetype>(context->cbCertEncoded));
            if (seen.contains(der))
                continue;
            seen.insert(der);
            if (QSslCertificate certificate(der, QSsl::Der); !certificate.isNull())
                out.append(certificate);
        }
        CertCloseStore(store, 0);
    }
#endif
    return out;
}

CaBundleLease::CaBundleLease(QString path, std::function<QList<QSslCertificate>()> roots)
    : m_path(std::move(path)), m_roots(std::move(roots))
{
    QFile::remove(m_path);
}

bool CaBundleLease::acquire()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!writeCaBundle(m_path, m_roots()))
        return false;
    ++m_users;
    return true;
}

void CaBundleLease::release()
{
    if (m_users > 0 && --m_users == 0)
        QFile::remove(m_path);
}

void lendRoots(AgentSpec &spec, const QString &folder, std::function<QList<QSslCertificate>()> roots)
{
    const QString path = folder + QStringLiteral("/roots.pem");
    auto lease = std::make_shared<CaBundleLease>(path, std::move(roots));
    if (spec.profile)
        spec.profile->read << folder;
    spec.environment.insert(QStringLiteral("CODEX_CA_CERTIFICATE"), QDir::toNativeSeparators(path));
    spec.before = [lease, before = std::move(spec.before)](std::function<void(QString)> go) {
        if (!lease->acquire())
            return go(QStringLiteral("HikariSub couldn't write this system's trusted roots for it"));
        if (!before)
            return go({});
        before([lease, go = std::move(go)](QString error) {
            if (!error.isEmpty())
                lease->release();
            go(std::move(error));
        });
    };
    spec.after = [lease, after = std::move(spec.after)] {
        if (after)
            after();
        lease->release();
    };
}

}
