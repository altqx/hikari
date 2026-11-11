#pragma once

#include "hikari/application/edit_session.h"

#include <expected>
#include <map>
#include <optional>

namespace hikari::application::retiming {

int msOf(core::DocumentTime t);
// Truncated towards zero to a multiple of 10 ms.
int zeroIt(int ms);

struct Retime {
    std::optional<int> startMs, endMs;
};

// One step named `name`; times clamp at 0 and a MicroDVD Document's frames follow
// its own rate. No change records nothing and yields false.
std::expected<bool, CommandRefusal> retime(EditSession &session, const char *name,
                                           const std::map<core::LineId, Retime> &times);

}
