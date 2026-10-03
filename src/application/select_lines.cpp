#include "hikari/application/select_lines.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/clipboard_rows.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace hikari::application {

namespace {

// SelectLines.h option bits.
enum : int {
    kContains = 1,
    kNotContains = 2,
    kMatchCase = 1 << 2,
    kRegularExpressions = 1 << 3,
    kFieldText = 1 << 4,
    kFieldStyle = 1 << 5,
    kFieldActor = 1 << 6,
    kFieldEffect = 1 << 7,
    kFieldStartTime = 1 << 8,
    kFieldEndTime = 1 << 9,
    kDialogues = 1 << 10,
    kComments = 1 << 11,
    kSelect = 1 << 12,
    kAddToSelection = 1 << 13,
    kDeselect = 1 << 14,
    kDoNothing = 1 << 15,
    kDoCopy = 1 << 16,
    kDoCut = 1 << 17,
    kDoMoveOnStart = 1 << 18,
    kDoMoveOnEnd = 1 << 19,
    kDoSetAssComment = 1 << 20,
    kDoDelete = 1 << 21,
};

std::u8string replaceAll(std::u8string s, std::u8string_view from, std::u8string_view to)
{
    for (std::size_t at = s.find(from); at != std::u8string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

std::u8string number(std::int64_t value)
{
    const std::string s = std::to_string(value);
    return {s.begin(), s.end()};
}

// SubsTime::raw for the Document's format (the Grid's time column).
std::u8string rawTime(const core::TimeField &time, std::optional<std::int64_t> frame, core::SubtitleFormat format)
{
    const std::int64_t ms = time.value.microseconds() / 1000;
    switch (format) {
    case core::SubtitleFormat::Srt: return core::legacy::srtTimeText(ms);
    case core::SubtitleFormat::TMPlayer: return core::legacy::tmpTimeText(ms);
    case core::SubtitleFormat::MicroDvd: {
        // A Line without its frame takes one at 25 fps (SubsTime::raw).
        std::int64_t f = frame.value_or(0);
        if (!f && ms)
            f = static_cast<std::int64_t>(std::ceil(static_cast<float>(ms) * (25.f / 1000.f)));
        return number(f);
    }
    case core::SubtitleFormat::Mpl2:
        return number(static_cast<std::int64_t>(std::ceil(static_cast<float>(ms) * (10.0f / 1000.0f))));
    default: return core::legacy::assTimeText(ms);
    }
}

std::u8string fieldText(const core::LineRecord &line, SelectLinesSettings::Field field, core::SubtitleFormat format,
                        bool translationMode)
{
    using F = SelectLinesSettings::Field;
    switch (field) {
    case F::Style: return line.style;
    case F::Actor: return line.actor;
    case F::Effect: return line.effect;
    case F::Start: return rawTime(line.start, line.startFrame, format);
    case F::End: return rawTime(line.end, line.endFrame, format);
    case F::Text: break;
    }
    return translationMode && !line.translation.empty() ? line.translation : line.text;
}

std::u16string asciiFold(std::u16string_view s)
{
    std::u16string out(s);
    for (auto &c : out)
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c - u'A' + u'a');
    return out;
}

} // namespace

SelectLinesSettings selectLinesFromOptions(int options)
{
    using S = SelectLinesSettings;
    S s;
    s.with = !(options & kNotContains);
    s.matchCase = options & kMatchCase;
    s.regex = options & kRegularExpressions;
    // The first field bit wins; none leaves the first radio button (Text).
    s.field = options & kFieldText        ? S::Field::Text
              : options & kFieldStyle     ? S::Field::Style
              : options & kFieldActor     ? S::Field::Actor
              : options & kFieldEffect    ? S::Field::Effect
              : options & kFieldStartTime ? S::Field::Start
              : options & kFieldEndTime   ? S::Field::End
                                          : S::Field::Text;
    s.dialogues = (options & kDialogues) || !(options & kComments);
    s.comments = options & kComments;
    s.mode = options & kAddToSelection ? S::Mode::AddToSelection
             : options & kDeselect     ? S::Mode::Deselect
                                       : S::Mode::Select;
    s.action = options & kDoCopy             ? S::Action::Copy
               : options & kDoCut            ? S::Action::Cut
               : options & kDoMoveOnStart    ? S::Action::MoveToBeginning
               : options & kDoMoveOnEnd      ? S::Action::MoveToEnd
               : options & kDoSetAssComment  ? S::Action::SetAsComment
               : options & kDoDelete         ? S::Action::Delete
                                             : S::Action::None;
    return s;
}

int selectLinesOptions(const SelectLinesSettings &s)
{
    using F = SelectLinesSettings::Field;
    int options = s.with ? kContains : kNotContains;
    if (s.matchCase)
        options |= kMatchCase;
    if (s.regex)
        options |= kRegularExpressions;
    switch (s.field) {
    case F::Text: options |= kFieldText; break;
    case F::Style: options |= kFieldStyle; break;
    case F::Actor: options |= kFieldActor; break;
    case F::Effect: options |= kFieldEffect; break;
    case F::Start: options |= kFieldStartTime; break;
    case F::End: options |= kFieldEndTime; break;
    }
    if (s.dialogues)
        options |= kDialogues;
    if (s.comments)
        options |= kComments;
    options |= kSelect << static_cast<int>(s.mode);
    options |= kDoNothing << static_cast<int>(s.action);
    return options;
}

std::vector<std::u8string> addRecentSelection(std::vector<std::u8string> recent, const std::u8string &text)
{
    // Legacy removes while counting up without stepping back, so of two
    // equal neighbours the second one stays.
    for (std::size_t i = 0; i < recent.size(); ++i)
        if (recent[i] == text)
            recent.erase(recent.begin() + static_cast<std::ptrdiff_t>(i));
    const std::size_t before = recent.size();
    recent.insert(recent.begin(), text);
    if (before > 20)
        recent.resize(20);
    return recent;
}

std::u8string stylesPattern(const std::vector<std::u8string> &styles)
{
    std::u8string s;
    for (std::size_t i = 0; i < styles.size(); ++i)
        s += (i ? u8"," : u8"") + styles[i];
    s = replaceAll(s, u8"\\", u8"\\\\");
    s = replaceAll(s, u8"|", u8"\\|");
    s = replaceAll(s, u8",", u8"|");
    for (const char8_t *c : {u8"[", u8"]", u8"(", u8")", u8"*", u8"+", u8"."})
        s = replaceAll(s, c, std::u8string(u8"\\") + c);
    return u8"^" + s + u8"$";
}

std::expected<SelectLinesResult, CommandRefusal> selectLines(EditSession &session, const SelectLinesSettings &settings,
                                                             const LineVisible &visible, const TextFold &fold)
{
    using A = SelectLinesSettings::Action;
    using M = SelectLinesSettings::Mode;
    const TextFold lower = fold ? fold : TextFold(asciiFold);
    const bool translation = translationMode(session);
    const core::Document &document = session.document();
    const auto lines = document.lines();

    Selection selection = session.selection();
    // SaveSelections(selectOptions == 0): Select starts from nothing.
    if (settings.mode == M::Select)
        selection.selected.clear();

    std::u16string find = core::toUtf16(settings.find);
    if (!settings.matchCase && !settings.regex)
        find = lower(find);
    // wxRegEx(find, wxRE_ADVANCED [| wxRE_ICASE]) (R1-pcre2).
    std::optional<core::LegacyRegex> re;
    if (settings.regex) {
        re.emplace(find, core::LegacyRegex::Advanced | (settings.matchCase ? 0 : core::LegacyRegex::IgnoreCase));
        if (!re->isValid()) {
            session.setSelection(selection);
            return SelectLinesResult{};
        }
    }

    SelectLinesResult result;
    std::vector<core::LineId> acted; // selected Lines the walk saw, in order
    for (const auto *line : lines) {
        if (visible && !visible(line->id))
            continue;
        std::u16string txt = core::toUtf16(fieldText(*line, settings.field, document.format(), translation));
        bool found = false;
        if (!txt.empty() && !find.empty()) {
            if (re) {
                found = re->matches(txt);
            } else {
                if (!settings.matchCase)
                    txt = lower(txt);
                found = txt.find(find) != std::u16string::npos;
            }
        } else if (find.empty() && txt.empty()) {
            found = true;
        }
        if (found == settings.with && ((settings.dialogues && !line->comment) || (settings.comments && line->comment))) {
            if (settings.mode != M::Deselect) {
                selection.selected.insert(line->id);
                ++result.count;
            } else if (selection.selected.erase(line->id)) {
                ++result.count;
            }
        }
        if (selection.selected.contains(line->id))
            acted.push_back(line->id);
    }

    const A action = settings.action;
    if (action == A::Copy || action == A::Cut)
        result.clipboard = core::clipboardRows(document, acted, translation, false);

    // The row the active Line has now: legacy keeps currentLine as a row.
    std::size_t activeRow = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (selection.active && lines[i]->id == *selection.active)
            activeRow = i;

    const bool removes = action == A::Cut || action == A::Delete || action == A::MoveToBeginning || action == A::MoveToEnd;
    std::vector<core::LineId> doomed; // every selected Line, hidden ones too (DeleteSelectedDialogues)
    if (removes)
        for (const auto *line : lines)
            if (selection.selected.contains(line->id))
                doomed.push_back(line->id);
    const bool moves = action == A::MoveToBeginning || action == A::MoveToEnd;
    const bool changes = removes ? !doomed.empty() : action == A::SetAsComment && !acted.empty();

    if (changes) {
        // Approved F2-move-hidden (2026-10-04): Move takes every selected
        // Line, hidden ones too, in Document order (legacy deleted the hidden
        // ones and inserted copies of the walked ones only).
        const std::vector<core::LineId> &moved = moves ? doomed : acted;
        const std::set<core::LineId> movedSet(moved.begin(), moved.end());
        std::set<core::LineId> touches(doomed.begin(), doomed.end());
        touches.insert(acted.begin(), acted.end());
        const auto ran = session.run(Command{
            "Selecting lines", session.revision(), touches, [&](core::Document &d) {
                if (action == A::SetAsComment) {
                    for (const auto id : acted)
                        if (!d.editLine(id, [](core::LineRecord &l) { l.comment = true; }))
                            return false;
                    return true;
                }
                // Moved Lines keep their LineIds.
                for (const auto id : doomed)
                    if (!(moves && movedSet.contains(id)) && !d.removeLine(id))
                        return false;
                if (moves) {
                    std::optional<core::LineId> before;
                    if (action == A::MoveToBeginning)
                        for (const auto *l : d.lines())
                            if (!movedSet.contains(l->id)) {
                                before = l->id;
                                break;
                            }
                    if (action == A::MoveToEnd || before)
                        for (const auto id : moved)
                            if (!d.moveLine(id, before))
                                return false;
                }
                if (d.lines().empty()) {
                    // Legacy adds a default Dialogue (0:00:00.00-0:00:05.00, Default).
                    core::LineRecord line;
                    line.style = u8"Default";
                    line.end.value = core::DocumentTime(5000 * 1000);
                    return d.appendLine(line).has_value();
                }
                return true;
            }});
        if (!ran)
            return std::unexpected(ran.error());
        if (moves)
            selection.selected = movedSet;
        else if (removes)
            selection.selected.clear(); // SaveSelections(true)
    }

    // The first shown selected Line becomes active; with none, the Line now
    // at the active row (or the last Line).
    const auto after = session.document().lines();
    std::optional<core::LineId> active;
    for (const auto *l : after)
        if (selection.selected.contains(l->id) && (!visible || visible(l->id))) {
            active = l->id;
            break;
        }
    if (!active && !after.empty())
        active = after[std::min(activeRow, after.size() - 1)]->id;
    for (auto it = selection.selected.begin(); it != selection.selected.end();)
        it = std::ranges::any_of(after, [&](const auto *l) { return l->id == *it; }) ? std::next(it)
                                                                                      : selection.selected.erase(it);
    if (active && active != session.selection().active && !session.navigateTo(*active))
        return std::unexpected(CommandRefusal::InvalidDraft);
    selection.active = active;
    selection.anchor = active;
    selection.extent.reset();
    session.setSelection(selection);
    return result;
}

} // namespace hikari::application
