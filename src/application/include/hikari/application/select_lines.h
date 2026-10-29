#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <functional>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct SelectLinesSettings {
    enum class Field { Text, Style, Actor, Effect, Start, End };
    enum class Mode { Select, AddToSelection, Deselect };
    enum class Action { None, Copy, Cut, MoveToBeginning, MoveToEnd, SetAsComment, Delete };

    std::u8string find;
    bool with = true;         // false: "Without"
    bool matchCase = false;
    bool regex = false;
    Field field = Field::Text;
    bool dialogues = true;
    bool comments = false;
    Mode mode = Mode::Select;
    Action action = Action::None;
};

// Option bits as HikariSub legacy stores them.
SelectLinesSettings selectLinesFromOptions(int options);
int selectLinesOptions(const SelectLinesSettings &settings);

// Moves `text` to the front.
// MUST KILL: cut to 20 only when it already had more, so a full list grows to 21
std::vector<std::u8string> addRecentSelection(std::vector<std::u8string> recent, const std::u8string &text);

// An anchored alternation; escapes only \ | [ ] ( ) * + .
std::u8string stylesPattern(const std::vector<std::u8string> &styles);

// For searches without "Match case".
using TextFold = std::function<std::u16string(std::u16string_view)>;

struct SelectLinesResult {
    int count = 0;                          // the number the message shows
    std::optional<std::u8string> clipboard; // Copy and Cut: what goes on the clipboard
    // The expression and PCRE2's message.
    std::optional<std::pair<std::u16string, std::u16string>> invalidExpression;
    // One per match that failed on a PCRE2 match or heap limit; that Line
    // counts as not matching.
    std::vector<std::u16string> matchErrors;
};

// One run on one Document. Hidden Lines are skipped unless `visible` is
// empty (Ignore filtering in some actions). An invalid regular expression
// counts nothing, is reported in the result and only clears the selection in
// Select mode.
std::expected<SelectLinesResult, CommandRefusal> selectLines(EditSession &session, const SelectLinesSettings &settings,
                                                             const LineVisible &visible = {},
                                                             const TextFold &fold = {});

} // namespace hikari::application
