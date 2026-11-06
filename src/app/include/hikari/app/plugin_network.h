#pragma once

#include "hikari/api/api.h"

#include <QObject>
#include <QUrl>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace hikari::app {

// The HTTPS requests HikariSub makes for plugins: only to the hosts a plugin's manifest declares,
// and none while plugin network access is turned off.
class PluginNetwork : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t kMaxBodyBytes = 16 * 1024 * 1024;
    static constexpr int kTimeoutMs = 30'000;
    static constexpr int kMaxRedirects = 5;

    // `allowed` reads the global switch at each request.
    PluginNetwork(QNetworkAccessManager &network, std::function<bool()> allowed, QObject *parent = nullptr);
    ~PluginNetwork() override;

    // Why a plugin that declares `hosts` may not reach `url`, or nothing.
    static std::optional<api::Error> refusal(const std::vector<std::string> &hosts, const QUrl &url);

    // params: {url, method, headers, body, encoding}. Replies {status, url, headers, body, encoding}
    // once, unless the plugin's requests are cancelled first.
    void fetch(const std::string &plugin, const std::vector<std::string> &hosts, const api::Json &params, api::Reply reply);
    // Aborts the plugin's open requests without replying.
    void cancel(const std::string &plugin);

private:
    struct Open;
    void finish(QNetworkReply *reply);

    QNetworkAccessManager &m_network;
    std::function<bool()> m_allowed;
    std::vector<std::unique_ptr<Open>> m_open;
};

}
