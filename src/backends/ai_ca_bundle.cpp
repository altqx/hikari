#include "hikari/backends/ai_ca_bundle.h"

#include <QSaveFile>

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

}
