#include "hikari/application/audio_devices.h"

#include <algorithm>

namespace hikari::application {

OutputRoute resolveOutputRoute(const std::vector<SystemOutputDevice> &devices, std::string_view chosenId)
{
    if (!chosenId.empty())
        if (const auto it = std::ranges::find(devices, chosenId, &SystemOutputDevice::id); it != devices.end())
            return {it->id, it->name, false};
    if (const auto it = std::ranges::find(devices, true, &SystemOutputDevice::isDefault); it != devices.end())
        return {it->id, it->name, true};
    if (!devices.empty())
        return {devices.front().id, devices.front().name, true};
    return {};
}

AudioOutputRouting::AudioOutputRouting(AudioDevicesPort &devices, std::function<std::string()> chosen, Moved moved)
    : m_devices(devices), m_chosen(std::move(chosen)), m_moved(std::move(moved))
{
    m_route = resolveOutputRoute(m_devices.outputs(), m_chosen());
    m_devices.setChangedCallback([this] { refresh(); });
}

AudioOutputRouting::~AudioOutputRouting()
{
    m_devices.setChangedCallback({});
}

void AudioOutputRouting::refresh()
{
    OutputRoute route = resolveOutputRoute(m_devices.outputs(), m_chosen());
    if (route == m_route)
        return;
    m_route = std::move(route);
    if (m_moved)
        m_moved(m_route);
}

std::vector<OutputDeviceChoice> outputDeviceChoices(const std::vector<SystemOutputDevice> &devices,
                                                    std::string_view chosenId, std::string_view chosenName)
{
    using Kind = OutputDeviceChoice::Kind;
    std::vector<OutputDeviceChoice> out{{Kind::SystemDefault, {}, {}}};
    for (const auto &device : devices)
        out.push_back({Kind::Device, device.id, device.name});
    if (!chosenId.empty() && std::ranges::find(devices, chosenId, &SystemOutputDevice::id) == devices.end())
        out.push_back({Kind::Missing, std::string(chosenId), std::string(chosenName.empty() ? chosenId : chosenName)});
    return out;
}

}
