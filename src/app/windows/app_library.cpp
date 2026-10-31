#include "hikari/app/composition.h"
#include "hikari/app/launcher.h"

#include <type_traits>

extern "C" __declspec(dllexport) int hikari_app_main(int argc, char **argv, hikari::app::launcher::Shown shown)
{
    static_assert(std::is_same_v<decltype(&hikari_app_main), hikari::app::launcher::Entry>);
    return hikari::app::run(argc, argv, hikari::app::StartupMode::Run, shown);
}
