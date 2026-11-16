#pragma once

#include "hikari/backends/ai_agent_broker.h"

#include <QList>
#include <QSslCertificate>
#include <QString>

#include <functional>

namespace hikari::backends::ai {

/// Replaces `path` with `certificates` as one PEM file; false when it can't be written.
bool writeCaBundle(const QString &path, const QList<QSslCertificate> &certificates);

/// The roots the current user's and the machine's Root stores trust, once each; empty off Windows.
QList<QSslCertificate> userAndMachineRoots();

/// A PEM of `roots()` at `path` while any agent process runs: each start writes it afresh, the last exit deletes it.
/// A file left at `path` goes when the lease is made.
class CaBundleLease {
public:
    CaBundleLease(QString path, std::function<QList<QSslCertificate>()> roots);
    /// False when there are no roots or the file can't be written.
    bool acquire();
    void release();

private:
    QString m_path;
    std::function<QList<QSslCertificate>()> m_roots;
    int m_users = 0;
};

/// Points `spec`'s agent at a PEM of `roots()` in `folder`, through CODEX_CA_CERTIFICATE: written afresh before each
/// process starts, readable but not writable to it, deleted when the last one exits.
void lendRoots(AgentSpec &spec, const QString &folder,
               std::function<QList<QSslCertificate>()> roots = userAndMachineRoots);

}
