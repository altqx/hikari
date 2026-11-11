#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/resample.h"

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
// timebase. Bake also gives each frame the \t, \fad and \fade values it has
// reached (bakeAnimations).
enum class FrameAnimations { Keep, Bake };
std::expected<void, CommandRefusal> splitIntoFrames(EditSession &session, const LegacyTimebase &timebase,
                                                    FrameAnimations animations, const LineVisible &visible = {});
// One Line's frames as splitIntoFrames makes them; `styles` empty: Keep.
std::vector<core::LineRecord> lineFrames(const core::LineRecord &line, const LegacyTimebase &timebase,
                                         const std::vector<core::StyleValues> &styles, Resolution resolution);

// Each shown selected Line becomes pieces of at most `count` frames (or
// seconds, ending on frame starts when there is a timebase), the first
// keeping the Line's start and the last its end. Each piece's \move, \t and
// \fad times are moved to its own start (retimeAnimations). Frames need a
// timebase.
enum class SplitUnit { Frames, Seconds };
std::expected<void, CommandRefusal> splitEvery(EditSession &session, const LegacyTimebase &timebase, int count,
                                               SplitUnit unit, const LineVisible &visible = {});
std::vector<core::LineRecord> linePieces(const core::LineRecord &line, const LegacyTimebase &timebase, int count,
                                         SplitUnit unit);

// Each shown selected Line becomes one positioned Line per character, word,
// wrap (\N) or tag block, measured with its Style and tags from its \pos or
// \move, else the Style's default position, with an \org at that position
// when the Line rotates. The first piece keeps the Line. A tag block piece
// carries every tag in effect for it, and a \k delay for the syllables
// before it.
enum class SplitText { Characters, Words, Wraps, TagBlocks };
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
// A piece per run of text with the blocks before it; trailing blocks join the last piece.
std::vector<std::u16string> splitByTagBlocks(std::u16string_view text);
// Three decimals, trailing zeros and point dropped.
std::u8string floatText(float value);
// The text with its first \move(...) (or \pos(...)) replaced by \pos(x,y) at
// `ms`, the move's times absolute (zero times meaning the Line's own).
std::u8string moveToPos(std::u8string_view text, std::int64_t lineStartMs, std::int64_t lineEndMs, int ms);
}

}
