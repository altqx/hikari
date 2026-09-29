#pragma once

namespace hikari::app {

enum class StartupMode {
    Run,                    // normal application lifetime
    ExitAfterWindowCreated, // tests: compose, create the main window, then quit
};

int run(int argc, char **argv, StartupMode mode);

} // namespace hikari::app
