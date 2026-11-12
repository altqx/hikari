#include "hikari/backends/qt_audio_devices.h"

#include <QAudioDevice>
#include <QMediaDevices>

namespace hikari::backends {

QtAudioDevices::QtAudioDevices(QObject *parent) : QObject(parent) {}

QtAudioDevices::~QtAudioDevices() = default;

void QtAudioDevices::watch() const
{
    if (m_devices)
        return;
    m_devices = std::make_unique<QMediaDevices>();
    connect(m_devices.get(), &QMediaDevices::audioOutputsChanged, this, [this] {
        if (m_changed)
            m_changed();
    });
}

std::vector<application::SystemOutputDevice> QtAudioDevices::outputs() const
{
    watch();
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
