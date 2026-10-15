#pragma once

// H2 (#125; decision 60, 2026-10-16, "Follow + device picker"): which output
// device the two audio paths play through, the PortAudio editor output (the
// audio box) and general playback (Qt Multimedia). Both follow the system's
// default output device unless Settings > Audio names a device; a named
// device that is not connected falls back to the default until it returns.
// The device list and its changes come through AudioDevicesPort (Qt's
// QMediaDevices in the application, a fake in tests). Plain C++, no Qt.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// One output device as the system lists it.
struct SystemOutputDevice {
    // Stable across restarts and replugging: the endpoint id on Windows, the
    // sound server's sink name on Linux (QAudioDevice::id).
    std::string id;
    std::string name; // as the system shows it (QAudioDevice::description)
    bool isDefault = false;

    friend bool operator==(const SystemOutputDevice &, const SystemOutputDevice &) = default;
};

// The system's output devices. `changed` runs on the owner's thread whenever
// a device is added or removed or the default output changes.
class AudioDevicesPort {
public:
    virtual ~AudioDevicesPort() = default;
    virtual std::vector<SystemOutputDevice> outputs() const = 0;
    virtual void setChangedCallback(std::function<void()> changed) = 0;
};

// A port with no devices that never changes (tests without sound, a host
// without Qt Multimedia's device list).
class NoAudioDevices final : public AudioDevicesPort {
public:
    std::vector<SystemOutputDevice> outputs() const override { return {}; }
    void setChangedCallback(std::function<void()>) override {}
};

// Where audio goes now.
struct OutputRoute {
    std::string id;   // the device's id; empty when no device is connected
    std::string name; // its name
    // True while following the system default: nothing is chosen, or the
    // chosen device is not connected.
    bool systemDefault = true;

    friend bool operator==(const OutputRoute &, const OutputRoute &) = default;
};

// The chosen device (`chosenId`, empty: the system default) when it is
// connected, else the default device (the first one when the system names
// none), else no device.
OutputRoute resolveOutputRoute(const std::vector<SystemOutputDevice> &devices, std::string_view chosenId);

// Follows the devices and the choice: `moved` runs whenever the route
// changes (the default changed while following it, the device in use went
// away, the chosen device came back, another device was chosen). A device
// added or removed elsewhere leaves the route, and the outputs, alone.
class AudioOutputRouting {
public:
    using Moved = std::function<void(const OutputRoute &)>;
    // `chosen` reads the setting (audio.outputDevice) when the route is resolved.
    AudioOutputRouting(AudioDevicesPort &devices, std::function<std::string()> chosen, Moved moved);
    ~AudioOutputRouting();
    AudioOutputRouting(const AudioOutputRouting &) = delete;
    AudioOutputRouting &operator=(const AudioOutputRouting &) = delete;

    const OutputRoute &route() const { return m_route; }
    std::vector<SystemOutputDevice> devices() const { return m_devices.outputs(); }
    // Resolves again (the setting changed); `moved` runs when the route did.
    void refresh();

private:
    AudioDevicesPort &m_devices;
    std::function<std::string()> m_chosen;
    Moved m_moved;
    OutputRoute m_route;
};

// One entry of Settings > Audio's device choice.
struct OutputDeviceChoice {
    enum class Kind {
        SystemDefault, // follows the default (stores an empty id)
        Device,        // a connected device
        Missing,       // the chosen device, not connected: listed so OK keeps it
    };
    Kind kind = Kind::SystemDefault;
    std::string id;
    std::string name;

    friend bool operator==(const OutputDeviceChoice &, const OutputDeviceChoice &) = default;
};

// "System default", then each connected device in the system's order, then
// the chosen one when it is not connected (by the name stored with it).
std::vector<OutputDeviceChoice> outputDeviceChoices(const std::vector<SystemOutputDevice> &devices,
                                                    std::string_view chosenId, std::string_view chosenName);

} // namespace hikari::application
