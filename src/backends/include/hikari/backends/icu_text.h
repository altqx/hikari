#pragma once

#include "hikari/application/text_commands.h"

namespace hikari::backends {

// ICU's root-locale case mapping and UAX #14 line breaking (dictionary-based for Thai, Lao,
// Khmer and Burmese).
application::text_commands::TextServices icuTextServices();

}
