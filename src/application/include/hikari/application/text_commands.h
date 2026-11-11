#pragma once

// AU20 text commands: line breaks, case, width and dialogue alternatives. Every edit leaves tag
// blocks, \N, \n, \h and drawings as they are.

#include "hikari/application/automation_services.h"
#include "hikari/application/grid_commands.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::text_commands {

struct TextServices {
    std::function<std::u16string(std::u16string_view)> upper, lower;
    // Offsets where a line may break (UAX #14), each strictly inside the text and never inside a
    // grapheme cluster.
    std::function<std::vector<std::size_t>(std::u16string_view)> lineBreaks;
};

enum class Case { Lower, Upper, Title, Sentence };
enum class Width { Full, Half };
enum class Direction { Left, Right };

std::u8string changeCase(std::u8string_view text, Case kind, const TextServices &services);
// Full: the bundled "Make fullwidth" demo's table. Half: U+FF01–FF5E and U+3000 back to ASCII,
// except ｛ ｝ ＼, which would become tags or escapes.
std::u8string changeWidth(std::u8string_view text, Width width);
// Flux.DialogSwapper's three replacements with the `*` delimiter.
std::u8string swapAlternatives(std::u8string_view text);

std::u8string removeBreaks(std::u8string_view text, const TextServices &services);
// Moves the first \N to the next or previous break point; with none, Right breaks after the first
// word and Left before the last.
std::u8string moveBreak(std::u8string_view text, Direction direction, const TextServices &services);
// `width` measures a prefix of the raw text, tags included. Existing breaks are removed first.
std::u8string balancedBreak(std::u8string_view text, const TextServices &services,
                            const std::function<double(std::u8string_view)> &width);

bool hasWrapStyle2(std::u8string_view text);
std::u8string setWrapStyle2(std::u8string_view text, bool on);

enum class Command {
    InsertBalancedBreak,
    MoveBreakLeft,
    MoveBreakRight,
    RemoveBreaks,
    ToggleWrapStyle2,
    Lowercase,
    Uppercase,
    TitleCase,
    SentenceCase,
    FullWidth,
    HalfWidth,
    SwapAlternatives,
};

// Each shown selected Line's text (its translation when it has one), as one undo step. Refused
// with no shown selected Line.
std::expected<void, CommandRefusal> run(EditSession &session, Command command, const TextServices &services,
                                        TextMeasurePort &measure, const LineVisible &visible = {});
// Every shown selected Line has \q2 in its start block.
bool selectionWrapsAtBreaksOnly(const EditSession &session, const LineVisible &visible = {});

}
