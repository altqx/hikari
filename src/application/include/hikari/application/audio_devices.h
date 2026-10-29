#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct SystemOutputDevice {
    // Stable across restarts and replugging.
    std::string id;
    std::string name;
    bool isDefault = false;

    friend bool operator==(const SystemOutputDevice &, const SystemOutputDevice &) = default;
};

// `changed` runs on the owner's thread when a device is added or removed or
// the default output changes.
class AudioDevicesPort {
public:
    virtual ~AudioDevicesPort() = default;
    virtual std::vector<SystemOutputDevice> outputs() const = 0;
    virtual void setChangedCallback(std::function<void()> changed) = 0;
};

class NoAudioDevices final : public AudioDevicesPort {
public:
    std::vector<SystemOutputDevice> outputs() const override { return {}; }
    void setChangedCallback(std::function<void()>) override {}
};

struct OutputRoute {
    std::string id;   // empty when no device is connected
    std::string name;
    // Also true while the chosen device is not connected.
    bool systemDefault = true;

    friend bool operator==(const OutputRoute &, const OutputRoute &) = default;
};

// The chosen device when connected, else the default (else the first), else
// none.
OutputRoute resolveOutputRoute(const std::vector<SystemOutputDevice> &devices, std::string_view chosenId);

// `moved` runs only when the route changes.
class AudioOutputRouting {
public:
    using Moved = std::function<void(const OutputRoute &)>;
    // `chosen` is read each time the route is resolved.
    AudioOutputRouting(AudioDevicesPort &devices, std::function<std::string()> chosen, Moved moved);
    ~AudioOutputRouting();
    AudioOutputRouting(const AudioOutputRouting &) = delete;
    AudioOutputRouting &operator=(const AudioOutputRouting &) = delete;

    const OutputRoute &route() const { return m_route; }
    std::vector<SystemOutputDevice> devices() const { return m_devices.outputs(); }
    // Call after the setting changes.
    void refresh();

private:
    AudioDevicesPort &m_devices;
    std::function<std::string()> m_chosen;
    Moved m_moved;
    OutputRoute m_route;
};

struct OutputDeviceChoice {
    enum class Kind {
        SystemDefault, // stores an empty id
        Device,
        Missing,       // chosen but not connected
    };
    Kind kind = Kind::SystemDefault;
    std::string id;
    std::string name;

    friend bool operator==(const OutputDeviceChoice &, const OutputDeviceChoice &) = default;
};

// System default, the connected devices, then the chosen one when missing.
std::vector<OutputDeviceChoice> outputDeviceChoices(const std::vector<SystemOutputDevice> &devices,
                                                    std::string_view chosenId, std::string_view chosenName);

} // namespace hikari::application
