#pragma once

// F2: select lines (legacy GLOBAL_OPEN_SELECT_LINES, the SelectLines dialog
// and SelectLines::SelectOnTab at 20d647c4).

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct SelectLinesSettings {
    enum class Field { Text, Style, Actor, Effect, Start, End };
    enum class Mode { Select, AddToSelection, Deselect };
    enum class Action { None, Copy, Cut, MoveToBeginning, MoveToEnd, SetAsComment, Delete };

    std::u8string find;
    bool with = true;         // "With"; false is "Without"
    bool matchCase = false;
    bool regex = false;
    Field field = Field::Text;
    bool dialogues = true;
    bool comments = false;
    Mode mode = Mode::Select;
    Action action = Action::None;
};

// Legacy SELECT_LINES_OPTIONS, read as the dialog does and written as
// SelectLines::SaveOptions does (SelectLines.h bits).
SelectLinesSettings selectLinesFromOptions(int options);
int selectLinesOptions(const SelectLinesSettings &settings);

// Legacy SELECT_LINES_RECENT_SELECTIONS after SelectLines::AddRecent: the
// text moves to the front; the list is cut to 20 only when it already had
// more than 20 before (so a full list grows to 21 until the dialog reopens).
std::vector<std::u8string> addRecentSelection(std::vector<std::u8string> recent, const std::u8string &text);

// The "+" button (SelectLines::OnChooseStyles): the checked styles as an
// anchored alternation, escaped the legacy way (\ | [ ] ( ) * + . only).
std::u8string stylesPattern(const std::vector<std::u8string> &styles);

// Case folding for searches without "Match case" (legacy wxString::MakeLower).
using TextFold = std::function<std::u16string(std::u16string_view)>;

struct SelectLinesResult {
    int count = 0;                          // legacy allSelections: the message's number
    std::optional<std::u8string> clipboard; // Copy and Cut: what goes on the clipboard
};

// One run on one Document. Hidden Lines are skipped unless `visible` is
// empty (Ignore filtering in some actions). An invalid regular expression
// counts nothing and only clears the selection in Select mode.
std::expected<SelectLinesResult, CommandRefusal> selectLines(EditSession &session, const SelectLinesSettings &settings,
                                                             const LineVisible &visible = {},
                                                             const TextFold &fold = {});

} // namespace hikari::application
