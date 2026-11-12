#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::core {
class Document;
}
namespace hikari::application {
class EditSession;
}

// The karaoke templater's template language as Karaoke Templater 2.1.7 (Aegisub 3.2.2) or karaOK's
// "Karaoke Templater mod" 2.1.7c reads and expands it, with HikariSub's KE1 problem placement. Lua stays behind
// callbacks.
namespace hikari::application::karaoke_templates {

// Stock: 2.1.7's rules. KaraOK: karaOK's, where `template line` is 2.1.7's pre-line and `lsyl` its line.
enum class Mode { Stock, KaraOK };

// The [Script Info] key that holds the mode, and its two values.
inline constexpr std::string_view kModeKey = "Karaoke Templater";
inline constexpr std::string_view kModeKaraOK = "karaOK";
inline constexpr std::string_view kModeStock = "Aegisub 3.2.2";

// A line template's per-unit part runs before each of these.
enum class Unit { None, Syl, Word, Char };

// util.headtail: the text before the first run of whitespace, and the text after it.
std::pair<std::string_view, std::string_view> headtail(std::string_view s);
std::string lower(std::string_view s);

// A template or code Line: its subtitle index once earlier fx Lines are gone, its number in the Grid.
struct Where {
    int index = 0;
    int number = 0;
    std::string kind, effect, style;
    friend bool operator==(const Where &, const Where &) = default;
};

// Bytes from 1 in the Line's own text.
struct At {
    std::size_t start = 0, length = 0;
    friend bool operator==(const At &, const At &) = default;
};

struct Problem {
    int level = 0;
    std::optional<Where> where;
    std::optional<At> at;
    std::string message;
};

struct Part {
    std::string text;
    Where where;
};

struct Template {
    bool code = false;
    std::string text; // the code, or the main template text
    std::string pre;
    std::optional<std::string> style;
    std::optional<std::string> stylegroup; // `style X`: the Line's Style name holds X
    Unit unit = Unit::None;
    double loops = 1;
    int layer = 0;
    bool addtext = true, keeptags = false, multi = false, isline = false, perchar = false, noblank = false;
    std::optional<std::string> fx, fxgroup, id;
    std::optional<Where> where, textWhere, preWhere;
    std::vector<Part> textParts, preParts;
};

// One template can sit in several lists, as a Lua table can.
using TemplateRef = std::shared_ptr<const Template>;

// Unnamed line templates come in file order, then named ones in the order they first appear.
struct Templates {
    std::vector<TemplateRef> once, line, syl, furi, word, chr, furichar;
};

struct SubsEntry {
    bool dialogue = false, comment = false;
    int layer = 0;
    std::string effect, text, style;
};

struct Parsed {
    Templates templates;
    std::vector<int> removed; // the fx Lines, 1-based subtitle indices before any removal
    std::vector<Problem> problems;
};

using ToNumber = std::function<std::optional<double>(std::string_view)>;

// The mode a mode line's value names, ignoring case and surrounding spaces.
std::optional<Mode> modeOfLine(std::string_view value);
// The mode line's value when it names a mode; otherwise karaOK when a template or code Line uses karaOK-only
// syntax, and Stock when none does.
Mode modeOf(std::optional<std::string_view> modeLine, const std::vector<SubsEntry> &subs);
// `withoutTemplates` when the Document has neither a mode line nor a template or code Line.
Mode documentMode(const core::Document &document, Mode withoutTemplates = Mode::Stock);
// karaOK for a Document File › New made, until a mode line or a template or code Line decides.
Mode documentMode(const EditSession &session);

// A 3.2.2 template Effect in karaOK syntax: `pre-line` becomes `line`, `line` `lsyl`, and `syl` with the `char`
// modifier `char`. Code Lines and the other words stay.
std::string karaOKEffect(std::string_view stockEffect);
// The reverse, for the words that have a 3.2.2 form.
std::string stockEffect(std::string_view karaOKEffect);

// parse_templates, parse_code and parse_template; `keepFx` leaves the generated Lines in place.
Parsed parseTemplates(const std::vector<SubsEntry> &subs, bool keepFx, const ToNumber &toNumber,
                      Mode mode = Mode::Stock);

using Value = std::variant<double, std::string>;
using Variables = std::map<std::string, Value, std::less<>>;

// As tostring writes the value.
std::string valueText(const Value &v);

struct Evaluated {
    enum class Kind { Text, Keep, ParseError, RuntimeError };
    Kind kind = Kind::Keep;
    std::string text; // the value's text, or the error
};

using Evaluate = std::function<Evaluated(const std::string &expression)>;
using Report = std::function<void(int level, const std::optional<Where> &, const std::optional<At> &,
                                  const std::string &message)>;

// run_text_template: $variables (when `variables` is given), then !expressions!.
std::string runTextTemplate(const std::string &text, const Variables *variables, const std::optional<Where> &where,
                            const std::vector<Part> *parts, const Evaluate &evaluate, const Report &report);

}
