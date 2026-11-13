#pragma once

#include <cstdarg>

namespace hikari::backends {

void logLibass(int level, const char *format, va_list args, void *);

}
