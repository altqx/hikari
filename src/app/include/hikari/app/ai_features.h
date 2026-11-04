#pragma once

#include "hikari/application/ai_providers.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace hikari::api {
class Api;
}
namespace hikari::backends::ai {
class AiRequests;
class AiSignIn;
struct CodexCli;
class AiSidecarDownload;
class AiSidecarHost;
}
namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

/// Every AI object the app builds, behind "Turn off all AI features". With the switch on, none exists: no key
/// store, config store or sidecar host, so nothing reads the keychain, downloads, starts or listens.
class AiFeatures final : public QObject {
    Q_OBJECT
public:
    static constexpr const char *kOffSetting = "ai.off";
    static constexpr const char *kCachingSetting = "ai.promptCaching";

    struct Ports {
        std::function<std::unique_ptr<application::ai::KeyStore>()> keyStore;
        std::function<std::unique_ptr<application::ai::ConfigStore>()> configStore;
        /// Null when no sidecar is installed for this version.
        std::function<std::unique_ptr<backends::ai::AiSidecarHost>()> sidecarHost;
        /// The download of this version's sidecar; null when one is installed or the build runs its own.
        std::function<std::unique_ptr<backends::ai::AiSidecarDownload>()> sidecarDownload;
        std::function<void(std::function<void(std::optional<backends::ai::CodexCli>)>)> findCodex;
        /// The Codex CLI's sign-in, which Import reads and never writes.
        QString codexAuthPath;
    };
    /// The keychain, providers.json and the sidecar under <userFolder>/ai.
    static Ports defaultPorts(const QString &userFolder, api::Api *api, const QString &appVersion);

    AiFeatures(ui::SettingsStore &settings, Ports ports, QObject *parent = nullptr);
    ~AiFeatures() override;

    bool on() const { return m_live != nullptr; }
    /// Null with AI off.
    application::ai::KeyStore *keys() const;
    application::ai::ConfigStore *configs() const;
    application::ai::Providers *providers() const;
    backends::ai::AiRequests *requests() const;
    backends::ai::AiSignIn *signIn() const;
    const Ports &ports() const { return m_ports; }
    /// Null with AI off, or when nothing needs downloading.
    std::unique_ptr<backends::ai::AiSidecarDownload> sidecarDownload() const;
    /// The sidecar for an AI action: built on the first one once a provider exists. Null with AI off, without a
    /// provider, or without an installed sidecar.
    backends::ai::AiSidecarHost *sidecarForAction();
    /// While the user adds their first provider, its Test connection may start the sidecar too.
    void setDrafting(bool drafting);
    /// The host if one was built; never builds it.
    backends::ai::AiSidecarHost *sidecar() const;

signals:
    void switched(bool on);

private:
    void apply();
    backends::ai::AiSidecarHost *sidecarForSignIn();

    struct Live;
    ui::SettingsStore &m_settings;
    Ports m_ports;
    std::unique_ptr<Live> m_live;
};

}
