#pragma once

#include "hikari/plugins/manifest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <set>

class QNetworkAccessManager;
class QNetworkReply;

namespace hikari::app {

// The programs plugins download, kept in <root>/<plugin id>/<sha256>/ once their size and SHA-256 check out.
class PluginProgramDownloads : public QObject {
    Q_OBJECT
public:
    struct Wiring {
        QString root;
        QNetworkAccessManager *network = nullptr;
        std::function<bool()> networkAllowed;
        int transferTimeoutMs = 60'000;
    };

    explicit PluginProgramDownloads(Wiring wiring, QObject *parent = nullptr);
    ~PluginProgramDownloads() override;

    // The program's file once kept; empty before.
    QString file(const std::string &pluginId, const plugins::ProgramRun &run) const;
    // This platform's downloads the manifest names that aren't kept yet.
    std::vector<const plugins::Program *> missing(const std::string &pluginId, const plugins::Manifest &manifest) const;
    // Starts each missing download that isn't running. With the network switch off it fails at once.
    void fetch(const std::string &pluginId, const plugins::Manifest &manifest);
    bool fetching(const std::string &pluginId) const;
    // Why the plugin's last download failed; empty when none did.
    QString problem(const std::string &pluginId) const;
    // Removes the plugin's kept downloads whose hash isn't in `hashes`.
    void keepOnly(const std::string &pluginId, const std::set<std::string> &hashes);
    void forget(const std::string &pluginId);
    // Forgets every plugin not in `installed`.
    void keepPlugins(const std::set<std::string> &installed);

    // This platform's download hashes of a manifest.
    static std::set<std::string> hashes(const plugins::Manifest &manifest);

signals:
    void changed(const QString &pluginId);

private:
    struct Fetch;
    QString folder(const std::string &pluginId) const;
    void finish(Fetch *fetch);
    QString place(const Fetch &fetch);
    void fail(const std::string &pluginId, const QString &problem);

    Wiring m_wiring;
    std::vector<std::unique_ptr<Fetch>> m_fetches;
    std::map<std::string, QString> m_problems;
};

}
