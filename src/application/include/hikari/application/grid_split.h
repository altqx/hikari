#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hikari::application {

// With exactly one shown selected Line, it ends at the end time of the frame
// at the video time (to centiseconds) and a copy starts there, after it. The
// time is not checked against the Line.
std::expected<void, CommandRefusal> splitAtVideoTime(EditSession &session, const LegacyTimebase &timebase,
                                                     std::int64_t videoMs, const LineVisible &visible = {});
// Each shown selected Line becomes one Line per frame from the frame at its
// Start to the one before the frame at its End (to centiseconds); a \move
// becomes the \pos it has at each frame's time. Needs an exact (indexed)
// timebase.
std::expected<void, CommandRefusal> splitIntoFrames(EditSession &session, const LegacyTimebase &timebase,
                                                    const LineVisible &visible = {});

// Each shown selected Line becomes one positioned Line per character, word or
// wrap (\N), measured with its Style and tags from its \pos or \move, else
// the Style's default position. The first piece keeps the Line.
enum class SplitText { Characters, Words, Wraps };
// Each segment's end offset and whether it is a word (letters, numbers, kana,
// ideographs).
using WordSegments = std::function<std::vector<std::pair<std::size_t, bool>>(std::u16string_view)>;
std::expected<void, CommandRefusal> splitByText(EditSession &session, SplitText kind, TextMeasurePort &measure,
                                                const WordSegments &words, const LineVisible &visible = {});

namespace legacy {
// On UTF-16 code units.
std::vector<std::u16string> splitByChar(std::u16string_view text, bool addSpaces = true);
std::vector<std::u16string> splitByWord(std::u16string_view text, const WordSegments &words);
std::vector<std::u16string> splitByWrap(std::u16string_view text);
// Three decimals, trailing zeros and point dropped.
std::u8string floatText(float value);
// The text with its first \move(...) (or \pos(...)) replaced by \pos(x,y) at
// `ms`, the move's times absolute (zero times meaning the Line's own).
std::u8string moveToPos(std::u8string_view text, std::int64_t lineStartMs, std::int64_t lineEndMs, int ms);
}

}
