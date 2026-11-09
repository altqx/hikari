#pragma once

#include "hikari/backends/ai_channel.h"
#include "hikari/minisign/minisign.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <map>
#include <optional>
#include <vector>

class QNetworkReply;

namespace hikari::backends::ai {

enum class SidecarDownloadError {
    Manifest,  // the channel manifest couldn't be fetched, its signature was refused, or it is older than one seen
    NoAsset,   // the manifest offers no sidecar this app runs on this platform
    Network,   // an asset couldn't be fetched
    Mismatch,  // its size or SHA-256 differs from the manifest's
    Unpack,    // it isn't a valid gzip stream or bundle archive
    Disk,
};

/// Where the channel is downloaded from: GitHub, or a local feed in tests.
struct SidecarSource {
    QString downloadBase; // ends with "/"; the channel's files are under ai-channel/
    bool github = true;
    // The keys the manifest's .minisig must verify with.
    std::vector<minisign::PublicKey> keys;
    static SidecarSource gitHub();
    static SidecarSource local(const QUrl &base, std::vector<minisign::PublicKey> keys = {});
};

/// A verified sidecar on disk: the runtime runs the bundle.
struct SidecarInstall {
    QString version;
    QString runtime;
    QString bundle;
    std::map<QString, QString> packages;
};

/// Keeps the AI sidecar under <root>: runtimes in runtime/<id>/, bundles in bundle/<version>/, and state.json with the
/// version in use, the previous one, a staged one and the versions skipped after failing. Checks the signed channel
/// manifest, picks the newest release this app runs and downloads it. A file that fails a check is deleted, never run.
class AiSidecarUpdater final : public QObject {
    Q_OBJECT
public:
    AiSidecarUpdater(QString root, SidecarCompat compat, SidecarSource source = SidecarSource::gitHub(),
                     QObject *parent = nullptr);
    ~AiSidecarUpdater() override;

    /// The sidecar to run: the current version unless it is blocked or skipped, then the previous one.
    std::optional<SidecarInstall> current() const;
    /// Downloaded and waiting for the switch.
    std::optional<SidecarInstall> staged() const;
    /// The version a rollback returns to; empty when there is none.
    QString previous() const;
    /// The newest version the last check offered, when it is newer than the current one.
    QString available() const { return m_available; }
    QDateTime lastCheck() const { return m_checked; }
    bool busy() const { return m_reply != nullptr; }

    /// Reads the manifest: what the first download would be, for the prompt. Emits described or failed.
    void describe();
    /// Downloads the newest sidecar when none can run, and makes it current. Emits installed or failed.
    void install();
    /// Reads the manifest, with a conditional GET; with `download`, stages a newer sidecar. Emits checked, then downloaded
    /// or failed.
    void check(bool download);
    /// Makes the staged sidecar current, keeping the old one as previous. False with nothing staged.
    bool switchToStaged();
    /// Skips `version` from now on; when it was current, the previous sidecar becomes current again.
    void reject(const QString &version);
    void abort();

signals:
    void described(qint64 bytes);
    void progress(qint64 received, qint64 total);
    void installed(hikari::backends::ai::SidecarInstall install);
    void downloaded(QString version);
    void checked();
    /// Whatever was downloaded is already deleted. `detail` is the network's own message, when there is one.
    void failed(hikari::backends::ai::SidecarDownloadError error, QString detail);

private:
    enum class Goal { Describe, Install, Check, Stage };
    struct Step {
        QString name;
        ChannelAsset asset;
        QString unpackInto;
        bool runtime = false;
    };

    QNetworkReply *get(const QUrl &url, qint64 cap, const QByteArray &etag = {});
    void fetchManifest(Goal goal);
    void fetchSignature(Goal goal, QByteArray manifest, QByteArray etag);
    void useManifest(Goal goal, const QByteArray &manifest, const QByteArray &signature);
    void runSteps(Goal goal, SidecarRelease release, std::vector<Step> steps);
    void finishStep(Goal goal, SidecarRelease release, std::vector<Step> steps);
    void finished(Goal goal, const SidecarRelease &release);
    void fail(SidecarDownloadError error, const QString &detail = {});
    void discard();

    std::optional<SidecarInstall> installation(const QString &version) const;
    QString bundleDirectory(const QString &version) const;
    QString runtimeDirectory(const QString &runtime) const;
    bool usable(const QString &version) const;
    void load();
    void save() const;
    void prune();

    QString m_root;
    SidecarCompat m_compat;
    SidecarSource m_source;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QFile m_part;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;

    QString m_current;
    QString m_previous;
    QString m_staged;
    QStringList m_skipped;
    QStringList m_blocked;
    QString m_seen;
    QString m_available;
    QByteArray m_etag;
    QDateTime m_checked;
};

}
