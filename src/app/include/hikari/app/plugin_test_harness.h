#pragma once

#include "hikari/application/update_check.h"

#include <cstdio>

namespace hikari::app {

// `hikarisub --test-plugin`: loads the folder as a dev plugin in a throwaway user folder, runs each step of the
// script against it and writes what failed to `out`. 0 when every step passed, 1 when one failed, 2 when the tests
// couldn't run. Needs a QGuiApplication.
int runPluginTests(const application::PluginTestArguments &arguments, std::FILE *out);

}
