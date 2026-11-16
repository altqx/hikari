#pragma once

#include <QList>
#include <QSslCertificate>
#include <QString>

namespace hikari::backends::ai {

/// Replaces `path` with `certificates` as one PEM file; false when it can't be written.
bool writeCaBundle(const QString &path, const QList<QSslCertificate> &certificates);

}
