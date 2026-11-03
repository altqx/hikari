#pragma once

#include "hikari/application/ai_providers.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace hikari::api {
class Api;
}
namespace hikari::backends::ai {
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

    struct Ports {
        std::function<std::unique_ptr<application::ai::KeyStore>()> keyStore;
        std::function<std::unique_ptr<application::ai::ConfigStore>()> configStore;
        /// Null when no sidecar is installed for this version.
        std::function<std::unique_ptr<backends::ai::AiSidecarHost>()> sidecarHost;
    };
    /// The keychain, providers.json and the sidecar under <userFolder>/ai.
    static Ports defaultPorts(const QString &userFolder, api::Api *api, const QString &appVersion);

    AiFeatures(ui::SettingsStore &settings, Ports ports, QObject *parent = nullptr);
    ~AiFeatures() override;

    bool on() const { return m_live != nullptr; }
    /// Null with AI off.
    application::ai::KeyStore *keys() const;
    application::ai::ConfigStore *configs() const;
    /// The sidecar for an AI action: built on the first one once a provider exists. Null with AI off, without a
    /// provider, or without an installed sidecar.
    backends::ai::AiSidecarHost *sidecarForAction();
    /// The host if one was built; never builds it.
    backends::ai::AiSidecarHost *sidecar() const;

signals:
    void switched(bool on);

private:
    void apply();

    struct Live;
    ui::SettingsStore &m_settings;
    Ports m_ports;
    std::unique_ptr<Live> m_live;
};

}
