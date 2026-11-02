#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>

namespace hikari::application {

// The centisecond nearest the middle between the start of the frame at or after
// `ms` (frameAt) and the start of the frame before it, so that a later shift by
// time moves every centered time by the same number of frames. `ms` itself when
// no centisecond lies inside that frame; 0 for the first frame.
int centeredTime(const LegacyTimebase &timebase, int ms);

// Centers the start and end times of every selected Line (hidden ones too) in one
// step; each stays on its frame. No change records nothing and still succeeds.
// The pending draft is committed first. Refused (Invalid) without a selected Line
// or with an empty timebase.
std::expected<void, CommandRefusal> centerTimesInFrames(EditSession &session, const LegacyTimebase &timebase);

}
