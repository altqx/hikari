#pragma once

#include "hikari/application/audio_devices.h"

#include <QMediaDevices>
#include <QObject>

namespace hikari::backends {

class QtAudioDevices final : public QObject, public application::AudioDevicesPort {
    Q_OBJECT
public:
    explicit QtAudioDevices(QObject *parent = nullptr);
    std::vector<application::SystemOutputDevice> outputs() const override;
    // Fires on device add/remove and default change, on the thread that made this object.
    void setChangedCallback(std::function<void()> changed) override;

private:
    QMediaDevices m_devices;
    std::function<void()> m_changed;
};

}
