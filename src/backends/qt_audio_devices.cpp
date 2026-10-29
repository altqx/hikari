#include "hikari/backends/qt_audio_devices.h"

#include <QAudioDevice>

namespace hikari::backends {

QtAudioDevices::QtAudioDevices(QObject *parent) : QObject(parent)
{
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        if (m_changed)
            m_changed();
    });
}

std::vector<application::SystemOutputDevice> QtAudioDevices::outputs() const
{
    std::vector<application::SystemOutputDevice> out;
    const QByteArray defaultId = QMediaDevices::defaultAudioOutput().id();
    for (const QAudioDevice &device : QMediaDevices::audioOutputs())
        out.push_back({device.id().toStdString(), device.description().toStdString(),
                       !defaultId.isEmpty() && device.id() == defaultId});
    return out;
}

void QtAudioDevices::setChangedCallback(std::function<void()> changed)
{
    m_changed = std::move(changed);
}

}
