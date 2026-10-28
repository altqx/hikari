#pragma once

// The Grid's split family (G4; legacy SubsGrid::Split at 20d647c4), one
// "Splitting lines" step each.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hikari::application {

// GRID_SPLIT_BY_VIDEO_TIME: with exactly one shown selected Line, it ends at
// the end time of the frame at the video time (EndTimeFor(FrameAt(t)), to
// centiseconds) and a copy starts there, after it. Legacy does not check that
// the time falls inside the Line (characterized).
std::expected<void, CommandRefusal> splitAtVideoTime(EditSession &session, const LegacyTimebase &timebase,
                                                     std::int64_t videoMs, const LineVisible &visible = {});
// GRID_SPLIT_BY_FRAME: each shown selected Line becomes one Line per frame
// from the frame at its Start to the one before the frame at its End, timed
// StartTimeFor/EndTimeFor (to centiseconds); a \move becomes the \pos it has
// at each frame's time. Needs an exact (indexed) timebase.
std::expected<void, CommandRefusal> splitIntoFrames(EditSession &session, const LegacyTimebase &timebase,
                                                    const LineVisible &visible = {});

// GRID_SPLIT_BY_CHARS / _WORDS / _WRAPS: each shown selected Line becomes
// one positioned Line per character, word or wrap (\N), placed by measuring
// the text with its Style and tags as legacy GetTaggedTextExtents does, from
// its \pos or \move, else the Style's default position at the script
// resolution (PlayResX/Y, with legacy's 1280x720 and 16:9 fallbacks). The
// first piece keeps the Line; the others follow it.
enum class SplitText { Characters, Words, Wraps };
// Word segmentation (legacy boost::locale boundaries): each segment's end
// offset and whether it is a word (letters, numbers, kana, ideographs).
using WordSegments = std::function<std::vector<std::pair<std::size_t, bool>>(std::u16string_view)>;
std::expected<void, CommandRefusal> splitByText(EditSession &session, SplitText kind, TextMeasurePort &measure,
                                                const WordSegments &words, const LineVisible &visible = {});

namespace legacy {
// Dialogue::SplitByChar / SplitByWord / SplitByWrap on one text (UTF-16 code
// units, as legacy's wxString on Windows).
std::vector<std::u16string> splitByChar(std::u16string_view text, bool addSpaces = true);
std::vector<std::u16string> splitByWord(std::u16string_view text, const WordSegments &words);
std::vector<std::u16string> splitByWrap(std::u16string_view text);
// getfloat(num, "5.3f", true): three decimals, trailing zeros and point dropped.
std::u8string floatText(float value);
// The text with its first \move(...) (or \pos(...)) replaced by \pos(x,y) at
// `ms`, the move's times absolute (zero times meaning the Line's own).
std::u8string moveToPos(std::u8string_view text, std::int64_t lineStartMs, std::int64_t lineEndMs, int ms);
} // namespace legacy

} // namespace hikari::application
