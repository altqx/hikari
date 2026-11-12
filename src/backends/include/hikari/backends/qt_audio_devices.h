#pragma once

#include "hikari/application/audio_devices.h"

#include <QObject>

#include <memory>

class QMediaDevices;

namespace hikari::backends {

// Loads Qt Multimedia's backend on the first outputs() call, not before.
class QtAudioDevices final : public QObject, public application::AudioDevicesPort {
    Q_OBJECT
public:
    explicit QtAudioDevices(QObject *parent = nullptr);
    ~QtAudioDevices() override;
    std::vector<application::SystemOutputDevice> outputs() const override;
    // Fires on device add/remove and default change, on the thread that made this object.
    void setChangedCallback(std::function<void()> changed) override;

private:
    void watch() const;

    mutable std::unique_ptr<QMediaDevices> m_devices;
    std::function<void()> m_changed;
};

}
