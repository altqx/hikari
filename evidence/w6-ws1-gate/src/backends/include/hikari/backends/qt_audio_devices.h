#pragma once

// H2 (decision 60): the system's output devices through Qt Multimedia's
// QMediaDevices: QAudioDevice::id (the endpoint id on Windows, the sound
// server's sink name on Linux), its description and the default. Qt emits
// audioOutputsChanged when a device is added or removed and when the default
// output changes (on Windows from its IMMNotificationClient), on the thread
// that made this object.

#include "hikari/application/audio_devices.h"

#include <QMediaDevices>
#include <QObject>

namespace hikari::backends {

class QtAudioDevices final : public QObject, public application::AudioDevicesPort {
    Q_OBJECT
public:
    explicit QtAudioDevices(QObject *parent = nullptr);
    std::vector<application::SystemOutputDevice> outputs() const override;
    void setChangedCallback(std::function<void()> changed) override;

private:
    QMediaDevices m_devices;
    std::function<void()> m_changed;
};

} // namespace hikari::backends
