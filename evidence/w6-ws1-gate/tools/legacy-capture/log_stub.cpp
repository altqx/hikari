// The legacy app's log window is GUI. SubsTime.cpp only reaches it through
// HikariLog(), which does nothing while no handler exists; the probe never
// creates one, so these definitions satisfy the linker without changing any
// observed result.
#include "LogHandler.h"
#include <cstdio>

LogHandler *LogHandler::sthis = nullptr;

void LogHandler::LogMessage(const wxString &format, bool)
{
    std::fprintf(stderr, "legacy log: %s\n", static_cast<const char *>(format.utf8_str()));
}
