// How legacy read the Style and Dialogue lines Demux built: the AssColor,
// Styles and Dialogue classes (styles.h, SubsDialogue.h) with
// Styles::parseStyle, Styles::GetRaw, Dialogue::SetRaw, Dialogue::GetRaw and
// what they reach (styles.cpp, SubsDialogue.cpp), copied unchanged into
// their own namespace so the stand-ins Demux constructs keep the legacy
// names. Included by standins.h after emptyString and the format enum.
#pragma once

#include <wx/colour.h>
#include <wx/regex.h>

#include <atomic>

namespace legacy_parse {
class ParseData;
#include "styles_classes.inc"
#include "dialogue_classes.inc"
} // namespace legacy_parse
