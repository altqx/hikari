// Starts the real composition root and exits once the main window exists.
#include "hikari/app/composition.h"

int main(int argc, char **argv)
{
    return hikari::app::run(argc, argv, hikari::app::StartupMode::ExitAfterWindowCreated);
}
