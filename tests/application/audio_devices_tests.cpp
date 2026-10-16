// H2 (#125, decision 60): the output device both audio paths follow. A fake
// device list stands in for the system's (QMediaDevices): the default
// changes, the device in use goes away, the chosen device is missing and
// comes back.
#include "hikari/application/audio_devices.h"

#include <gtest/gtest.h>

using namespace hikari::application;

namespace {

class FakeDevices : public AudioDevicesPort {
public:
    std::vector<SystemOutputDevice> list;
    std::function<void()> changed;

    std::vector<SystemOutputDevice> outputs() const override { return list; }
    void setChangedCallback(std::function<void()> callback) override { changed = std::move(callback); }

    void makeDefault(const std::string &id)
    {
        for (auto &d : list)
            d.isDefault = d.id == id;
        notify();
    }
    void remove(const std::string &id)
    {
        std::erase_if(list, [&](const SystemOutputDevice &d) { return d.id == id; });
        notify();
    }
    void add(SystemOutputDevice device)
    {
        list.push_back(std::move(device));
        notify();
    }
    void notify()
    {
        if (changed)
            changed();
    }
};

const SystemOutputDevice monitor{"{0.0.0.00000000}.{F2906D39}", "22E1W (NVIDIA High Definition Audio)", true};
const SystemOutputDevice headset{"{0.0.0.00000000}.{C244E1FF}", "Speakers (Razer BlackShark V2 X USB)", false};
const SystemOutputDevice spdif{"{0.0.0.00000000}.{BC5B95E8}", "Realtek Digital Output (Realtek(R) Audio)", false};

struct Routing : ::testing::Test {
    FakeDevices devices;
    std::string chosen;
    std::vector<OutputRoute> moves;
    void SetUp() override { devices.list = {monitor, headset}; }
    AudioOutputRouting make()
    {
        return AudioOutputRouting(devices, [this] { return chosen; }, [this](const OutputRoute &r) { moves.push_back(r); });
    }
};

} // namespace

TEST(OutputRoute, FollowsTheDefaultUnlessADeviceIsChosen)
{
    const std::vector<SystemOutputDevice> list{monitor, headset};
    EXPECT_EQ(resolveOutputRoute(list, ""), (OutputRoute{monitor.id, monitor.name, true}));
    EXPECT_EQ(resolveOutputRoute(list, headset.id), (OutputRoute{headset.id, headset.name, false}));
    // the chosen device is not connected: the default
    EXPECT_EQ(resolveOutputRoute(list, spdif.id), (OutputRoute{monitor.id, monitor.name, true}));
    // no default named: the first device; no device at all: none
    auto noDefault = list;
    noDefault[0].isDefault = false;
    EXPECT_EQ(resolveOutputRoute(noDefault, ""), (OutputRoute{monitor.id, monitor.name, true}));
    EXPECT_EQ(resolveOutputRoute({}, headset.id), (OutputRoute{"", "", true}));
}

// The default changes while nothing is chosen: the outputs move to the new
// default (A9/A10/B6 in the H2 evidence stayed on the old device).
TEST_F(Routing, ADefaultChangeMovesTheOutputs)
{
    auto routing = make();
    EXPECT_EQ(routing.route(), (OutputRoute{monitor.id, monitor.name, true}));
    EXPECT_TRUE(moves.empty()); // the first route is the outputs' own start
    devices.makeDefault(headset.id);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves.back(), (OutputRoute{headset.id, headset.name, true}));
    // a notification that changes nothing moves nothing
    devices.notify();
    EXPECT_EQ(moves.size(), 1u);
}

// The device in use goes away (A11, B7): the outputs move to the default the
// system picks.
TEST_F(Routing, RemovingTheDeviceInUseMovesToTheNewDefault)
{
    auto routing = make();
    devices.list[0].isDefault = false;
    devices.list[1].isDefault = true;
    devices.remove(monitor.id);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves.back(), (OutputRoute{headset.id, headset.name, true}));
    // the last device goes too: no device
    devices.remove(headset.id);
    EXPECT_EQ(moves.back(), (OutputRoute{"", "", true}));
}

// Another device added or removed leaves the route alone.
TEST_F(Routing, OtherDevicesComingAndGoingMoveNothing)
{
    auto routing = make();
    devices.add(spdif);
    devices.remove(spdif.id);
    devices.remove(headset.id);
    EXPECT_TRUE(moves.empty());
}

// A chosen device: kept when the default changes, left for the default while
// it is unplugged, taken again when it returns.
TEST_F(Routing, AChosenDeviceStaysAndReturns)
{
    chosen = headset.id;
    auto routing = make();
    EXPECT_EQ(routing.route(), (OutputRoute{headset.id, headset.name, false}));
    devices.add(spdif);
    devices.makeDefault(spdif.id);
    EXPECT_TRUE(moves.empty());
    devices.remove(headset.id);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves.back(), (OutputRoute{spdif.id, spdif.name, true}));
    devices.add(headset);
    ASSERT_EQ(moves.size(), 2u);
    EXPECT_EQ(moves.back(), (OutputRoute{headset.id, headset.name, false}));
}

// The chosen device is missing at start: the default, then the device when
// it is plugged in.
TEST_F(Routing, AChosenDeviceMissingAtStartFallsBackToTheDefault)
{
    chosen = spdif.id;
    auto routing = make();
    EXPECT_EQ(routing.route(), (OutputRoute{monitor.id, monitor.name, true}));
    devices.add(spdif);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves.back(), (OutputRoute{spdif.id, spdif.name, false}));
}

// The setting changes: refresh() moves the outputs.
TEST_F(Routing, ChoosingAnotherDeviceMovesTheOutputs)
{
    auto routing = make();
    chosen = headset.id;
    routing.refresh();
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves.back(), (OutputRoute{headset.id, headset.name, false}));
    chosen.clear();
    routing.refresh();
    EXPECT_EQ(moves.back(), (OutputRoute{monitor.id, monitor.name, true}));
    routing.refresh();
    EXPECT_EQ(moves.size(), 2u);
}

// The routing stops listening when it goes.
TEST_F(Routing, NoCallbackAfterTheRoutingGoes)
{
    {
        auto routing = make();
    }
    EXPECT_FALSE(devices.changed); // unregistered
    devices.makeDefault(headset.id);
    EXPECT_TRUE(moves.empty());
}

// Settings > Audio's choice: "System default", the connected devices, and a
// chosen device that is not connected, so that OK keeps it.
TEST(OutputDeviceChoices, ListsTheDefaultTheDevicesAndAMissingChoice)
{
    using Kind = OutputDeviceChoice::Kind;
    const std::vector<SystemOutputDevice> list{monitor, headset};
    const std::vector<OutputDeviceChoice> connected{{Kind::SystemDefault, "", ""},
                                                    {Kind::Device, monitor.id, monitor.name},
                                                    {Kind::Device, headset.id, headset.name}};
    EXPECT_EQ(outputDeviceChoices(list, "", ""), connected);
    EXPECT_EQ(outputDeviceChoices(list, headset.id, headset.name), connected);
    auto missing = connected;
    missing.push_back({Kind::Missing, spdif.id, spdif.name});
    EXPECT_EQ(outputDeviceChoices(list, spdif.id, spdif.name), missing);
    // a missing device stored without its name shows its id
    missing.back().name = spdif.id;
    EXPECT_EQ(outputDeviceChoices(list, spdif.id, ""), missing);
    EXPECT_EQ(outputDeviceChoices({}, "", ""), (std::vector<OutputDeviceChoice>{{Kind::SystemDefault, "", ""}}));
}
